#ifndef __USER_HASHER_H__
#define __USER_HASHER_H__
#include "AssetEncryption.h"

#include <memory>

class UserHasher {
public:
   UserHasher(const BinaryData& iv = SecureBinaryData());
   std::shared_ptr<KeyDerivationFunction> getKDF();
   std::string deriveKey(const std::string& rawData);
private:
   SecureBinaryData iv_;
   std::shared_ptr<KeyDerivationFunction> kdf_;
};

#endif //__USER_HASHER_H__
