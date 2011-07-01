#ifndef PUBLICKEY_H
#define PUBLICKKEY_H
#include <string>
#include <vector>

std::vector<std::string> publicKeyFile;

#define CLIENTNAME "license"
void fillKeyVector(){
publicKeyFile.push_back("-----BEGIN PUBLIC KEY-----");
publicKeyFile.push_back("MIIBtjCCASsGByqGSM44BAEwggEeAoGBAP1/U4EddRIpUt9KnC7s5Of2EbdSPO9E");
publicKeyFile.push_back("AMMeP4C2USZpRV1AIlH7WT2NWPq/xfW6MPbLm1Vs14E7gB00b/JmYLdrmVClpJ+f");
publicKeyFile.push_back("6AR7ECLCT7up1/63xhv4O1fnxqimFQ8E+4P208UewwI1VBNaFpEy9nXzrith1yrv");
publicKeyFile.push_back("8iIDGZ3RSAHHAhUAl2BQjxUjC8yykrmCouuEC/BYHPUCgYBGlgNRLjAnjNOUdZXb");
publicKeyFile.push_back("Iu7JgmpjIq3Jc0T0HXQMMlckyPnvuqfU2AP/jGCdzRAOvFvfz618akJfrqeG6iBQ");
publicKeyFile.push_back("6+mDUeof2h/fJNaUeqa5qiN2aVOAL019So7LoG0Zdookkf+xbQ75xDqZtfcWcv9v");
publicKeyFile.push_back("CiS0RNBzbQTTihoTItr2zdiMnQOBhAACgYAmSpBpnKuYaO4YCebaKLi5ouR9TjGW");
publicKeyFile.push_back("790b6Lk9slC/Y0ADPcwVwL3OMAMi7Wyw47hFdMm6DAFG4la1aJVyFLgtP+yYEpT3");
publicKeyFile.push_back("us5ugGFm9PMLK7TYt9ooPAs2oGfuGa78MccRLQPK0C+wFgvFoRB/OSdECCbgxy0I");
publicKeyFile.push_back("gayPTi/+G1uD8Q==");
publicKeyFile.push_back("-----END PUBLIC KEY-----");
publicKeyFile.push_back("");
}
#endif
