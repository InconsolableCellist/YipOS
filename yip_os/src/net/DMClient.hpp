#pragma once

#include <string>
#include <vector>
#include <cstdint>

typedef void CURL;

namespace YipOS {

struct DMMessage {
    std::string from_id;    // sender user_id
    std::string from_name;  // sender display name
    std::string text;
    int64_t date = 0;
    bool seen = false;
    bool is_mine = false;   // true if from_id == our user_id
};

struct DMSession {
    std::string session_id;
    std::string peer_id;
    std::string peer_name;
    std::vector<DMMessage> messages;
    int64_t last_seen_date = 0;
    bool has_unseen = false;
};

// Pairing state machine
enum class PairState {
    IDLE,
    CREATING,       // POST /dm/pair/create sent
    WAITING,        // code generated, waiting for peer to join
    JOINED,         // peer joined, awaiting our confirm
    CONFIRMING,     // POST /dm/pair/confirm sent
    COMPLETE,       // both confirmed, session ready
    JOINING,        // POST /dm/pair/join sent (receiver side)
    FAILED
};

struct PairInfo {
    PairState state = PairState::IDLE;
    std::string session_id;
    std::string code;
    std::string peer_name;
    std::string error;
    double expires_at = 0;  // monotonic time when code expires
};

class DMClient {
public:
    DMClient();
    ~DMClient();

    void SetEndpoint(const std::string& url);
    void SetUserId(const std::string& uid) { user_id_ = uid; }
    const std::string& GetUserId() const { return user_id_; }
    void SetDisplayName(const std::string& name) { display_name_ = name; }

    // --- Pairing ---
    // Initiator: create a new pair session, returns 6-digit code
    bool PairCreate(std::string& out_code, std::string& out_session_id);
    // Receiver: join using 6-digit code
    bool PairJoin(const std::string& code, std::string& out_session_id, std::string& out_peer_name);
    // Poll pairing status (initiator waits for peer to join)
    bool PairStatus(const std::string& session_id, std::string& out_status, std::string& out_peer_name);
    // Confirm pairing (both sides)
    bool PairConfirm(const std::string& session_id);

    // --- Messaging ---
    bool FetchMessages(const std::string& session_id, int64_t since = 0);
    bool SendMessage(const std::string& session_id, const std::string& text);

    // --- Session management ---
    void AddSession(const std::string& session_id, const std::string& peer_id, const std::string& peer_name);
    void RemoveSession(const std::string& session_id);
    std::vector<DMSession>& GetSessions() { return sessions_; }
    const std::vector<DMSession>& GetSessions() const { return sessions_; }
    DMSession* GetSession(const std::string& session_id);

    // --- Unseen ---
    bool HasUnseen() const;
    void MarkSessionSeen(const std::string& session_id, int64_t newest_date);

    // Poll all active sessions for new messages
    void PollAll();

    // Pairing state (for UI)
    PairInfo& GetPairInfo() { return pair_info_; }
    const PairInfo& GetPairInfo() const { return pair_info_; }

private:
    std::string PostJSON(const std::string& path, const std::string& body);
    std::string Get(const std::string& url);
    bool ParseMessages(const std::string& json, std::vector<DMMessage>& out);
    bool ParseSimpleJSON(const std::string& json, std::vector<std::pair<std::string, std::string>>& out);

    std::string endpoint_;
    std::string user_id_;
    std::string display_name_;
    std::vector<DMSession> sessions_;
    PairInfo pair_info_;
    CURL* curl_ = nullptr;
};

} // namespace YipOS
