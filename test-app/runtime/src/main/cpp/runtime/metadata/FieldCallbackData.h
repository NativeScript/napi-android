#ifndef FIELDCALLBACKDATA_H_
#define FIELDCALLBACKDATA_H_

#include "jni.h"
#include "js_native_api.h"
#include "MetadataEntry.h"

namespace tns {
    class ObjectManager;

    struct FieldCallbackData {
        FieldCallbackData(MetadataEntry metadata)
                :
                metadata(metadata), fid(nullptr), clazz(nullptr) {

        }

        MetadataEntry metadata;
        jfieldID fid;
        jclass clazz;
        // Cached prototype the accessor lives on; used to detect
        // Class.prototype.<field> access when host objects are disabled.
        napi_ref prototype = nullptr;
        // Cached per-env ObjectManager (this data is created per env, so the
        // pointer's lifetime matches it) — avoids a locked env->runtime lookup.
        tns::ObjectManager *objectManager = nullptr;
    };

}

#endif /* FIELDCALLBACKDATA_H_ */
