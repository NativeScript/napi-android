#ifndef WORKER_MESSAGE_H_
#define WORKER_MESSAGE_H_

#include <string>

namespace tns {
namespace worker {

// A worker message carried across the C++ inbox/task rails.
//
// This napi (multi-engine) port uses JSON string payloads (matching the fork's
// existing worker semantics) rather than V8's structured clone. Data messages
// hold a JSON string; Error messages carry the fields the parent's onerror
// handler needs.
enum class MessageKind { Data, Error };

struct Message {
    MessageKind kind = MessageKind::Data;

    // Data payload (JSON string produced by JsonStringifyObject / consumed by JSON.parse).
    std::string data;

    // Error fields (kind == Error).
    std::string errorMessage;
    std::string errorStackTrace;
    std::string errorFilename;
    int errorLineNo = 0;

    Message() = default;

    static Message MakeData(std::string json) {
        Message m;
        m.kind = MessageKind::Data;
        m.data = std::move(json);
        return m;
    }

    static Message MakeError(std::string message, std::string stackTrace,
                             std::string filename, int lineNo) {
        Message m;
        m.kind = MessageKind::Error;
        m.errorMessage = std::move(message);
        m.errorStackTrace = std::move(stackTrace);
        m.errorFilename = std::move(filename);
        m.errorLineNo = lineNo;
        return m;
    }
};

}  // namespace worker
}  // namespace tns

#endif /* WORKER_MESSAGE_H_ */
