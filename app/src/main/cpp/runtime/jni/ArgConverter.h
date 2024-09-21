#include "JEnv.h"
#include <string>
#include <map>

namespace  ns {
    class ArgConverter {

    public:

        static std::string jstringToString(jstring value) {
            if (value == nullptr) {
                return {};
            }

            JEnv env;

            jboolean f = JNI_FALSE;
            auto chars = env.GetStringUTFChars(value, &f);
            std::string s(chars);
            env.ReleaseStringUTFChars(value, chars);

            return s;
        }

    };
}
