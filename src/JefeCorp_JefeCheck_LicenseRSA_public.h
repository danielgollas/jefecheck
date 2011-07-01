#ifndef PUBLICKEYRSA_H
#define PUBLICKEYRSA_H
#include <string>
#include <vector>

//this is a RSA public key

std::vector<std::string> publicKeyRSA;

void fillPublicRSAVector(){
publicKeyRSA.push_back("-----BEGIN PUBLIC KEY-----");
publicKeyRSA.push_back("MIGfMA0GCSqGSIb3DQEBAQUAA4GNADCBiQKBgQDmgMCGY/BvnzF/1TldueKNjVHc");
publicKeyRSA.push_back("CkTQ18yrzjN5fxPP24U9NeSQlHYp90Ybyqcg0FydfA8iPh2R7ZuAH4ons+Srz4zB");
publicKeyRSA.push_back("3fGjVURCHQaXSreP/kmoYJ99gM/qcx89E6Ag++mgFUywr/JMfWfJSqnHNO/WrVLI");
publicKeyRSA.push_back("IqdeOwuT37bQ9PnDQQIDAQAB");
publicKeyRSA.push_back("-----END PUBLIC KEY-----");
}
#endif
