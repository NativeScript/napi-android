#ifndef FIELDCALLBACKDATA_H_
#define FIELDCALLBACKDATA_H_

#include "jni.h"
#include "MetadataEntry.h"

namespace ns {
struct FieldCallbackData {
    FieldCallbackData(MetadataEntry *metadata)
        :
        fid(nullptr), clazz(nullptr), metadata(metadata) {

    }

    MetadataEntry* metadata;
    jfieldID fid;
    jclass clazz;
};

}

#endif /* FIELDCALLBACKDATA_H_ */
