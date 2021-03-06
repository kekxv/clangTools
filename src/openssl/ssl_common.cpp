#include <ssl_common.h>

std::mutex ssl_common::is_init_ssl_lock;
bool ssl_common::is_init_ssl = false;

#ifdef __FILENAME__
const char *ssl_common::TAG = __FILENAME__;
#else
const char * ssl_common::TAG = "ssl_common";
#endif
ssl_common::ssl_common(const char *cert_file, const char *key_file) {
    logger::instance()->d(TAG, __LINE__, "initialising SSL");

#ifdef ENABLE_OPENSSL
    std::unique_lock<std::mutex> lock{is_init_ssl_lock};
    if (!is_init_ssl) {
        is_init_ssl = true;
        /* SSL library initialisation */
        SSL_library_init();
        OpenSSL_add_all_algorithms();
        SSL_load_error_strings();
        ERR_load_BIO_strings();
        ERR_load_crypto_strings();
    }
    lock.unlock();

    /* create the SSL server context */
    ctx = SSL_CTX_new(SSLv23_method());
    if (!ctx) {
        logger::instance()->e(TAG, __LINE__, "SSL_CTX_new()");
        return;
    }

    /* Load certificate and private key files, and check consistency */
    if (cert_file && key_file) {
        if (SSL_CTX_use_certificate_file(ctx, cert_file, SSL_FILETYPE_PEM) != 1) {
            logger::instance()->e(TAG, __LINE__, "SSL_CTX_use_certificate_file failed");
        }

        if (SSL_CTX_use_PrivateKey_file(ctx, key_file, SSL_FILETYPE_PEM) != 1) {
            logger::instance()->e(TAG, __LINE__, "SSL_CTX_use_PrivateKey_file failed");
        }

        /* Make sure the key and certificate file match. */
        if (SSL_CTX_check_private_key(ctx) != 1) {
            logger::instance()->e(TAG, __LINE__, "SSL_CTX_check_private_key failed");
        } else
            logger::instance()->d(TAG, __LINE__, "certificate and private key loaded and verified");
    }

    /* Recommended to avoid SSLv2 & SSLv3 */
    SSL_CTX_set_options(ctx, SSL_OP_ALL | SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3);
#else
    logger::instance()->e(TAG, __LINE__, "OpenSSL is disable!!!");
#endif
}

bool ssl_common::isReady() {
#ifdef ENABLE_OPENSSL
    return ctx != nullptr;
#else
	return false;
#endif
}

ssl_common::~ssl_common() {
#ifdef ENABLE_OPENSSL
    if (ctx) {
        SSL_CTX_free(ctx);
        ctx = nullptr;
    }
#endif
}

bool ssl_common::init_sub(ssl_common::ssl_common_sub *&sub) {
    std::unique_lock<std::mutex> lock(ssl_lock);
    if (sub != nullptr) {
        delete sub;
        sub = nullptr;
    }
    sub = new ssl_common_sub(this);
    return sub != nullptr;
}

void ssl_common::free_sub(ssl_common::ssl_common_sub *&sub) {
    std::unique_lock<std::mutex> lock(ssl_lock);
    if (sub != nullptr) {
        delete sub;
        sub = nullptr;
    }
}


ssl_common::ssl_common_sub::~ssl_common_sub() {
#ifdef ENABLE_OPENSSL
    if (ssl != nullptr) {
        SSL_shutdown(ssl);
//        SSL_free(ssl);
        ssl = nullptr;
    }
    if (rBIO != nullptr) {
        BIO_free(rBIO);
        rBIO = nullptr;
    } /* SSL reads from, we write to. */
    if (wBIO != nullptr) {
        BIO_free(wBIO);
        wBIO = nullptr;
    } /* SSL writes to, we read from. */
#endif
}

ssl_common::ssl_common_sub::ssl_common_sub(ssl_common *parent) {
    this->parent = parent;

#ifdef ENABLE_OPENSSL
    rBIO = BIO_new(BIO_s_mem());
    wBIO = BIO_new(BIO_s_mem());
    ssl = SSL_new(parent->ctx);

    if (parent->ssl_mode == SSL_MODE_SERVER)
        SSL_set_accept_state(ssl);  /* ssl server mode */
    else if (parent->ssl_mode == SSL_MODE_CLIENT)
        SSL_set_connect_state(ssl); /* ssl client mode */

    SSL_set_bio(ssl, rBIO, wBIO);
#endif
}

ssl_common::SSL_status ssl_common::ssl_common_sub::get_ssl_status(int n) {
#ifdef ENABLE_OPENSSL
    switch (SSL_get_error(ssl, n)) {
        case SSL_ERROR_NONE:
            return SSL_STATUS_OK;
        case SSL_ERROR_WANT_WRITE:
        case SSL_ERROR_WANT_READ:
            return SSL_STATUS_WANT_IO;
        case SSL_ERROR_ZERO_RETURN:
        case SSL_ERROR_SYSCALL:
        default:
            return SSL_STATUS_FAIL;
    }
#else
    return SSL_STATUS_FAIL;
#endif
}

void ssl_common::ssl_common_sub::send_unencrypted_bytes(unsigned char *buf, size_t len) {
    encrypt_buf.insert(encrypt_buf.end(), &buf[0], &buf[len]);
}

void ssl_common::ssl_common_sub::queue_encrypted_bytes(unsigned char *buf, size_t len) {
    write_buf.insert(write_buf.end(), &buf[0], &buf[len]);
}

ssl_common::SSL_status ssl_common::ssl_common_sub::do_ssl_handshake() {
#ifdef ENABLE_OPENSSL
    enum SSL_status status = SSL_STATUS_OK;

    int n = SSL_do_handshake(ssl);
    if (n >= 0)return status;
    unsigned char* buf = new unsigned char[parent->DEFAULT_BUF_SIZE];
    status = get_ssl_status(n);

    /* Did SSL request to write bytes? */
    if (status == SSL_STATUS_WANT_IO) {
        do {
            n = BIO_read(wBIO, buf, parent->DEFAULT_BUF_SIZE);
            if (n > 0)
                queue_encrypted_bytes(buf, n);
			else if (!BIO_should_retry(wBIO)) {
				delete[] buf;
				return SSL_STATUS_FAIL;
			}
        } while (n > 0);
    }
	delete[] buf;
	return status;
#else
    return SSL_status::SSL_STATUS_FAIL;
#endif

}

int ssl_common::ssl_common_sub::on_read_cb(std::vector<unsigned char> &data, unsigned char *src, size_t len) {
    write_buf.clear();
    enum SSL_status status = SSL_status::SSL_STATUS_OK;
#ifdef ENABLE_OPENSSL
	unsigned char* buf = new unsigned char[parent->DEFAULT_BUF_SIZE];
    int n = 0;

    while (len > 0) {
        n = BIO_write(rBIO, src, len);
		if (n <= 0) {
			delete[] buf;
			return -1; /* assume bio write failure is unrecoverable */
		}
        src += n;
        len -= n;
        if (!SSL_is_init_finished(ssl)) {
            if (do_ssl_handshake() == SSL_STATUS_FAIL) {
				delete[] buf;
				return -1;
			}
            if (!SSL_is_init_finished(ssl)) {
				delete[] buf;
				return 0;
			}
        }
        do {
            n = SSL_read(ssl, buf, parent->DEFAULT_BUF_SIZE);
            if (n > 0) {
                // TODO
                // client.io_on_read(buf, (size_t) n);
            }
        } while (n > 0);
        status = get_ssl_status(n);
        /* Did SSL request to write bytes? This can happen if peer has requested SSL
         * renegotiation. */
        if (status == SSL_STATUS_WANT_IO)
            do {
                n = BIO_read(wBIO, buf, parent->DEFAULT_BUF_SIZE);
                if (n > 0)
                    queue_encrypted_bytes(buf, n);
				else if (!BIO_should_retry(wBIO)) {
					delete[] buf;
					return -1;
				}
            } while (n > 0);

			if (status == SSL_STATUS_FAIL) {
				delete[] buf;
				return -1;
			}
    }
    data.insert(data.end(), write_buf.begin(), write_buf.end());
    write_buf.clear();
	delete[] buf;
#else
    return -1;
#endif
    return 0;
}

int ssl_common::ssl_common_sub::do_encrypt(std::vector<unsigned char> &encrypt_data, std::vector<unsigned char> data) {
    enum SSL_status status = SSL_status::SSL_STATUS_OK;

#ifdef ENABLE_OPENSSL
    if (!SSL_is_init_finished(ssl))
        return 0;
	unsigned char* buf = new unsigned char[parent->DEFAULT_BUF_SIZE];

    while (!data.empty()) {
        int n = SSL_write(ssl, data.data(), data.size());
        status = get_ssl_status(n);

        if (n > 0) {
            /* consume the waiting bytes that have been used by SSL */
            data.erase(data.begin(), data.begin() + n);

            /* take the output of the SSL object and queue it for socket write */
            do {
                n = BIO_read(wBIO, buf, parent->DEFAULT_BUF_SIZE);
                if (n > 0)
                    encrypt_data.insert(encrypt_data.end(), &buf[0], &buf[n]);
				else if (!BIO_should_retry(wBIO)) {
					delete[]buf;
					return -1;
				}
            } while (n > 0);
        }

		if (status == SSL_STATUS_FAIL) {
			delete[] buf;
			return -1;
		}

        if (n == 0)
            break;
    }
	delete[] buf;
#else
    return -1;
#endif
    return 0;
}

bool ssl_common::ssl_common_sub::SSL_init_finished() {
#ifdef ENABLE_OPENSSL
    return SSL_is_init_finished(ssl);
#else
    return false;
#endif
}


