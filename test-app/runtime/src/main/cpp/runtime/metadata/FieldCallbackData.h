#ifndef FIELDCALLBACKDATA_H_
#define FIELDCALLBACKDATA_H_

#include "jni.h"
#include "MetadataEntry.h"

namespace tns {
    struct FieldCallbackData {
        FieldCallbackData(MetadataEntry metadata)
                :
                metadata(metadata), fid(nullptr), clazz(nullptr), prototype(nullptr),
                ownsPrototype(false) {

        }

        MetadataEntry metadata;
        jfieldID fid;
        jclass clazz;
        napi_ref prototype;
        bool ownsPrototype;
    };

}

#endif /* FIELDCALLBACKDATA_H_ */
