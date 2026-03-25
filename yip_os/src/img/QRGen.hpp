#pragma once

#include <array>
#include <vector>
#include <string>
#include <cstdint>

namespace YipOS {

// QR Version 1 (21x21) numeric encoder.
// Generates the module matrix for a short numeric payload (up to 17 digits at EC-H).
// Designed for DM pairing codes (6-digit numeric strings).
class QRGen {
public:
    static constexpr int SIZE = 21;
    static constexpr int GRID_SIZE = 32;
    static constexpr int OFFSET = (GRID_SIZE - SIZE) / 2;  // 5

    // Module position for CRT bitmap writes (grid coordinates, not QR coordinates)
    struct Module {
        int col;  // grid column (0-31)
        int row;  // grid row (0-31)
    };

    // Encode a numeric string into a QR V1 matrix.
    // Returns true on success. Payload must be 1-17 digits (EC level H).
    bool Encode(const std::string& numeric_payload);

    // After Encode(), get the light modules to write on the CRT.
    // Template macro stamps finders/timing/quiet zone with data area = dark.
    // These are the modules that need to be set to VQ_WHITE (255).
    const std::vector<Module>& GetLightModules() const { return light_modules_; }

    // Get the full 21x21 matrix (true = dark module)
    const std::array<std::array<bool, SIZE>, SIZE>& GetMatrix() const { return matrix_; }

private:
    // QR V1-H parameters
    static constexpr int DATA_CODEWORDS = 9;
    static constexpr int EC_CODEWORDS = 17;
    static constexpr int TOTAL_CODEWORDS = 26;

    // Encoding steps
    void EncodeData(const std::string& payload);
    void ComputeEC();
    void PlaceBits();
    void ApplyMask(int mask_pattern);
    void ComputeFormatInfo(int mask_pattern);
    int EvaluateMask() const;

    // Reed-Solomon helpers
    static uint8_t GFMul(uint8_t a, uint8_t b);
    static uint8_t GFPow(uint8_t base, int exp);
    static void RSEncode(const uint8_t* data, int data_len, uint8_t* ec, int ec_len);

    // Fixed pattern detection
    static bool IsFixedModule(int row, int col);
    static bool IsFormatModule(int row, int col);

    // State
    std::array<std::array<bool, SIZE>, SIZE> matrix_{};
    std::array<std::array<bool, SIZE>, SIZE> placed_{};  // tracks which cells have been placed
    std::array<uint8_t, TOTAL_CODEWORDS> codewords_{};
    std::vector<Module> light_modules_;

    // GF(2^8) tables
    static constexpr int GF_SIZE = 256;
    static uint8_t gf_exp_[512];
    static uint8_t gf_log_[256];
    static bool gf_initialized_;
    static void InitGF();
};

} // namespace YipOS
