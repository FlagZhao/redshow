#ifndef REDSHOW_HASH_H
#define REDSHOW_HASH_H

#include <string>

namespace redshow {

namespace hash {

// Interface
std::string sha256(void *input, unsigned int length);

class SHA256 {
 protected:
  typedef unsigned char uint8;
  typedef unsigned int uint32;
  typedef unsigned long long uint64;

  const static uint32 sha256_k[];
  static const unsigned int SHA224_256_BLOCK_SIZE = (512 / 8);

 public:
  void init();
  void update(const unsigned char *message, unsigned int len);
  void final(unsigned char *digest);
  static const unsigned int DIGEST_SIZE = (256 / 8);

 protected:
  void transform(const unsigned char *message, unsigned int block_nb);
  unsigned int m_tot_len;
  unsigned int m_len;
  unsigned char m_block[2 * SHA224_256_BLOCK_SIZE];
  uint32 m_h[8];
};

}  // namespace hash

}  // namespace redshow

#endif