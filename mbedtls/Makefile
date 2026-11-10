
BASECFLAGS= 
CC=$(cc)
CXX=$(cxx)
ar=$(AR)
#-D USE_OPENSSL
BASEDIR= $(BaseDir) -I./include
LDFLAGS=$(BaseLibDir) 
#c++
CXXFLAGS=$(CompileOpion) $(CompileOpionCpp) $(BASECFLAGS) $(BASEDIR)
#c 
CFLAGS=$(CompileOpion) $(BASECFLAGS) $(BASEDIR) 
CPPFLAGS= $(CXXFLAGS)
#汇编
ASFLAGS=


#vtls
SRCS = ./library/aes.o \
	./library/aesni.o \
	./library/arc4.o \
	./library/aria.o \
	./library/asn1parse.o \
	./library/asn1write.o \
	./library/base64.o \
	./library/bignum.o \
	./library/blowfish.o \
	./library/camellia.o \
	./library/ccm.o \
	./library/certs.o \
	./library/chacha20.o \
	./library/chachapoly.o \
	./library/cipher.o \
	./library/cipher_wrap.o \
	./library/cmac.o \
	./library/constant_time.o \
	./library/ctr_drbg.o \
	./library/debug.o \
	./library/des.o \
	./library/dhm.o \
	./library/ecdh.o \
	./library/ecdsa.o \
	./library/ecjpake.o \
	./library/ecp.o \
	./library/ecp_curves.o \
	./library/entropy.o \
	./library/entropy_poll.o \
	./library/error.o \
	./library/gcm.o \
	./library/havege.o \
	./library/hkdf.o \
	./library/hmac_drbg.o \
	./library/md.o \
	./library/md2.o \
	./library/md4.o \
	./library/md5.o \
	./library/memory_buffer_alloc.o \
	./library/mps_reader.o \
	./library/mps_trace.o \
	./library/nist_kw.o \
	./library/oid.o \
	./library/padlock.o \
	./library/pem.o \
	./library/pk.o \
	./library/pkcs11.o \
	./library/pkcs12.o \
	./library/pkcs5.o \
	./library/pkparse.o \
	./library/pkwrite.o \
	./library/pk_wrap.o \
	./library/platform.o \
	./library/platform_util.o \
	./library/poly1305.o \
	./library/psa_crypto.o \
	./library/psa_crypto_aead.o \
	./library/psa_crypto_cipher.o \
	./library/psa_crypto_client.o \
	./library/psa_crypto_driver_wrappers.o \
	./library/psa_crypto_ecp.o \
	./library/psa_crypto_hash.o \
	./library/psa_crypto_mac.o \
	./library/psa_crypto_rsa.o \
	./library/psa_crypto_se.o \
	./library/psa_crypto_slot_management.o \
	./library/psa_crypto_storage.o \
	./library/psa_its_file.o \
	./library/ripemd160.o \
	./library/rsa.o \
	./library/rsa_internal.o \
	./library/sha1.o \
	./library/sha256.o \
	./library/sha512.o \
	./library/ssl_cache.o \
	./library/ssl_ciphersuites.o \
	./library/ssl_cli.o \
	./library/ssl_cookie.o \
	./library/ssl_msg.o \
	./library/ssl_srv.o \
	./library/ssl_ticket.o \
	./library/ssl_tls.o \
	./library/ssl_tls13_keys.o \
	./library/threading.o \
	./library/version.o \
	./library/version_features.o \
	./library/x509.o \
	./library/x509write_csr.o \
	./library/x509_create.o \
	./library/x509_crl.o \
	./library/x509_crt.o \
	./library/x509_csr.o \
	./library/xtea.o

ifneq ($(DevType),BK7258/)
SRCS+= ./library/net_sockets.o \
	./library/timing.o \
	./library/x509write_crt.o 
endif

 TARGET= libmbedtls.a
.PHONY: all clean  
   
all:$(TARGET)

$(TARGET): $(SRCPPS)  $(SRCS)
	ar rcs $(TARGET)   $(SRCS)
	mkdir -p ../output/$(DevType)$(CpuType)lib/
	cp -f $(TARGET) ../output/$(DevType)$(CpuType)lib/
	@echo '$(TARGET) done'

$(SRCS):%o:%c
	$(CC)  $(CFLAGS) $(LDFLAGS) $(DefHideFun)  -ffunction-sections -fdata-sections -Wl,--gc-sections -c $< -o $@  
clean:
	rm -f *.o $(TARGET)  $(SRCPPS) $(SRCS) output/$(DevType)$(CpuType)lib/$(TARGET)
