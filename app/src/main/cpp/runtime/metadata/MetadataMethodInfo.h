#ifndef METHODINFOSMARTPOINTER_H_
#define METHODINFOSMARTPOINTER_H_

#include <iostream>
#include <string>
#include <vector>

using namespace std;

namespace ns {
    class MethodInfo {
    public:

        MethodInfo(uint8_t *pValue)
                : isStatic(false), paramCount(0), m_pData(pValue), m_pStartData(pValue), m_signatureLength(0),
                  sizeMeasured(false), nameOffset(0), resolvedData(0),
                  declaringTypePtr(nullptr){
        }

        std::string GetName();

        uint8_t CheckIsResolved();

        uint16_t GetSignatureLength();

        std::string GetSignature();

        std::string GetDeclaringType(); //used only for static methods

        int GetSizeOfReadMethodInfo();

        bool isStatic;
        int paramCount;

    private:
        uint8_t *m_pData; //where we currently read
        uint8_t *m_pStartData;  // pointer to the beginning
        uint16_t m_signatureLength;
        bool sizeMeasured;

        uint8_t nameOffset;
        uint8_t resolvedData;
        uint16_t *declaringTypePtr;
        std::vector<uint16_t> nodeIds;


    };
}

#endif /* METHODINFOSMARTPOINTER_H_ */
