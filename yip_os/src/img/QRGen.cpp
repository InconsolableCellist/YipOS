#include "QRGen.hpp"
#include <algorithm>
#include <cstring>

namespace YipOS {

// GF(2^8) tables — initialized once
uint8_t QRGen::gf_exp_[512];
uint8_t QRGen::gf_log_[256];
bool QRGen::gf_initialized_ = false;

void QRGen::InitGF() {
    if (gf_initialized_) return;
    // QR uses primitive polynomial 0x11D (x^8 + x^4 + x^3 + x^2 + 1)
    int val = 1;
    for (int i = 0; i < 255; i++) {
        gf_exp_[i] = static_cast<uint8_t>(val);
        gf_log_[val] = static_cast<uint8_t>(i);
        val <<= 1;
        if (val >= 256) val ^= 0x11D;
    }
    // Extend exp table for easy modular access
    for (int i = 255; i < 512; i++) {
        gf_exp_[i] = gf_exp_[i - 255];
    }
    gf_log_[0] = 0;  // undefined, but avoid garbage
    gf_initialized_ = true;
}

uint8_t QRGen::GFMul(uint8_t a, uint8_t b) {
    if (a == 0 || b == 0) return 0;
    return gf_exp_[gf_log_[a] + gf_log_[b]];
}

uint8_t QRGen::GFPow(uint8_t base, int exp) {
    uint8_t result = 1;
    for (int i = 0; i < exp; i++) result = GFMul(result, base);
    return result;
}

// Reed-Solomon encoding: compute EC codewords from data codewords.
// Generator polynomial for ec_len EC codewords:
//   g(x) = (x - α^0)(x - α^1)...(x - α^(ec_len-1))
void QRGen::RSEncode(const uint8_t* data, int data_len, uint8_t* ec, int ec_len) {
    // Build generator polynomial coefficients
    // gen[i] = coefficient of x^i, degree = ec_len
    std::vector<uint8_t> gen(ec_len + 1, 0);
    gen[0] = 1;
    for (int i = 0; i < ec_len; i++) {
        // Multiply gen by (x - α^i) = (x + α^i) in GF(2^8)
        std::vector<uint8_t> next(ec_len + 1, 0);
        uint8_t alpha_i = gf_exp_[i];
        for (int j = ec_len; j >= 0; j--) {
            if (j > 0) next[j] ^= gen[j - 1];  // x * gen[j-1]
            next[j] ^= GFMul(gen[j], alpha_i);   // α^i * gen[j]
        }
        gen = next;
    }

    // Polynomial division: data * x^ec_len mod gen
    std::vector<uint8_t> remainder(ec_len, 0);
    for (int i = 0; i < data_len; i++) {
        uint8_t coeff = data[i] ^ remainder[0];
        // Shift remainder left
        for (int j = 0; j < ec_len - 1; j++) {
            remainder[j] = remainder[j + 1];
        }
        remainder[ec_len - 1] = 0;
        // Subtract coeff * gen
        for (int j = 0; j < ec_len; j++) {
            remainder[j] ^= GFMul(coeff, gen[ec_len - 1 - j]);
        }
    }

    for (int i = 0; i < ec_len; i++) {
        ec[i] = remainder[i];
    }
}

bool QRGen::IsFixedModule(int row, int col) {
    // Finder patterns + separators (3 corners, 8x8 each)
    if (row < 8 && col < 8) return true;       // top-left
    if (row < 8 && col >= SIZE - 8) return true; // top-right
    if (row >= SIZE - 8 && col < 8) return true;  // bottom-left

    // Timing patterns
    if (row == 6 || col == 6) return true;

    // Dark module
    if (row == SIZE - 8 && col == 8) return true;

    // Format info
    if (IsFormatModule(row, col)) return true;

    return false;
}

bool QRGen::IsFormatModule(int row, int col) {
    // Format info around top-left finder
    if (row == 8 && col <= 8) return true;
    if (col == 8 && row <= 8) return true;
    // Format info at bottom-left and top-right
    if (col == 8 && row >= SIZE - 7) return true;
    if (row == 8 && col >= SIZE - 8) return true;
    return false;
}

// --- Encoding ---

bool QRGen::Encode(const std::string& payload) {
    InitGF();

    // Validate: numeric only, max 17 digits for V1-H
    if (payload.empty() || payload.size() > 17) return false;
    for (char c : payload) {
        if (c < '0' || c > '9') return false;
    }

    // Clear state
    for (auto& row : matrix_) row.fill(false);
    for (auto& row : placed_) row.fill(false);
    std::memset(codewords_.data(), 0, codewords_.size());
    light_modules_.clear();

    // Step 1: Encode data into codewords
    EncodeData(payload);

    // Step 2: Compute Reed-Solomon EC
    ComputeEC();

    // Step 3: Place fixed patterns in matrix
    // Finder patterns (3 corners)
    auto placeFinder = [&](int r0, int c0) {
        for (int r = 0; r < 7; r++) {
            for (int c = 0; c < 7; c++) {
                bool dark = (r == 0 || r == 6 || c == 0 || c == 6 ||
                            (r >= 2 && r <= 4 && c >= 2 && c <= 4));
                matrix_[r0 + r][c0 + c] = dark;
                placed_[r0 + r][c0 + c] = true;
            }
        }
    };
    placeFinder(0, 0);
    placeFinder(0, SIZE - 7);
    placeFinder(SIZE - 7, 0);

    // Separators (white border around finders)
    for (int i = 0; i < 8; i++) {
        // Top-left
        placed_[7][i] = true; placed_[i][7] = true;
        // Top-right
        placed_[7][SIZE - 8 + i] = true; placed_[i][SIZE - 8] = true;
        // Bottom-left
        placed_[SIZE - 8][i] = true; placed_[SIZE - 8 + i][7] = true;
    }

    // Timing patterns
    for (int i = 8; i < SIZE - 8; i++) {
        matrix_[6][i] = (i % 2 == 0);
        placed_[6][i] = true;
        matrix_[i][6] = (i % 2 == 0);
        placed_[i][6] = true;
    }

    // Dark module
    matrix_[SIZE - 8][8] = true;
    placed_[SIZE - 8][8] = true;

    // Mark format info areas as placed (will be written after masking)
    for (int i = 0; i <= 8; i++) {
        placed_[8][i] = true;
        placed_[i][8] = true;
    }
    for (int i = SIZE - 7; i < SIZE; i++) {
        placed_[i][8] = true;
    }
    for (int i = SIZE - 8; i < SIZE; i++) {
        placed_[8][i] = true;
    }

    // Step 4: Place data + EC bits
    PlaceBits();

    // Step 5: Try all 8 mask patterns, pick best
    std::array<std::array<bool, SIZE>, SIZE> base_matrix = matrix_;
    int best_mask = 0;
    int best_penalty = 999999;

    for (int m = 0; m < 8; m++) {
        matrix_ = base_matrix;
        ApplyMask(m);
        ComputeFormatInfo(m);
        int penalty = EvaluateMask();
        if (penalty < best_penalty) {
            best_penalty = penalty;
            best_mask = m;
        }
    }

    // Apply best mask
    matrix_ = base_matrix;
    ApplyMask(best_mask);
    ComputeFormatInfo(best_mask);

    // Step 6: Extract light modules for CRT rendering
    // After template stamp, data area is all dark. We write light modules.
    // Also write format info modules (template may have different format info).
    light_modules_.clear();
    for (int r = 0; r < SIZE; r++) {
        for (int c = 0; c < SIZE; c++) {
            // Skip finder patterns, separators, timing — template handles those
            bool is_finder_or_separator =
                (r < 8 && c < 8) ||
                (r < 8 && c >= SIZE - 8) ||
                (r >= SIZE - 8 && c < 8);
            bool is_timing = (r == 6 && c >= 8 && c < SIZE - 8) ||
                             (c == 6 && r >= 8 && r < SIZE - 8);
            bool is_dark_module = (r == SIZE - 8 && c == 8);

            if (is_finder_or_separator || is_timing || is_dark_module) continue;

            // For format info and data modules: write if light
            if (!matrix_[r][c]) {
                light_modules_.push_back({c + OFFSET, r + OFFSET});
            }
        }
    }

    return true;
}

void QRGen::EncodeData(const std::string& payload) {
    // Build bit stream: mode(4) + count(10) + data + terminator(4) + padding
    std::vector<bool> bits;

    // Mode indicator: numeric = 0001
    bits.push_back(false); bits.push_back(false);
    bits.push_back(false); bits.push_back(true);

    // Character count (10 bits for V1 numeric)
    int count = static_cast<int>(payload.size());
    for (int i = 9; i >= 0; i--) {
        bits.push_back((count >> i) & 1);
    }

    // Numeric data: groups of 3 digits → 10 bits, group of 2 → 7 bits, 1 → 4 bits
    int i = 0;
    while (i < count) {
        int remaining = count - i;
        if (remaining >= 3) {
            int val = (payload[i] - '0') * 100 + (payload[i+1] - '0') * 10 + (payload[i+2] - '0');
            for (int b = 9; b >= 0; b--) bits.push_back((val >> b) & 1);
            i += 3;
        } else if (remaining == 2) {
            int val = (payload[i] - '0') * 10 + (payload[i+1] - '0');
            for (int b = 6; b >= 0; b--) bits.push_back((val >> b) & 1);
            i += 2;
        } else {
            int val = payload[i] - '0';
            for (int b = 3; b >= 0; b--) bits.push_back((val >> b) & 1);
            i += 1;
        }
    }

    // Terminator (up to 4 bits, don't exceed capacity)
    int capacity_bits = DATA_CODEWORDS * 8;
    int term_len = std::min(4, capacity_bits - static_cast<int>(bits.size()));
    for (int t = 0; t < term_len; t++) bits.push_back(false);

    // Pad to byte boundary
    while (bits.size() % 8 != 0) bits.push_back(false);

    // Pad with alternating 11101100 / 00010001
    uint8_t pad_bytes[] = {0xEC, 0x11};
    int pad_idx = 0;
    while (static_cast<int>(bits.size()) < capacity_bits) {
        uint8_t pb = pad_bytes[pad_idx % 2];
        for (int b = 7; b >= 0; b--) bits.push_back((pb >> b) & 1);
        pad_idx++;
    }

    // Convert to codewords
    for (int cw = 0; cw < DATA_CODEWORDS; cw++) {
        uint8_t byte = 0;
        for (int b = 0; b < 8; b++) {
            byte = (byte << 1) | (bits[cw * 8 + b] ? 1 : 0);
        }
        codewords_[cw] = byte;
    }
}

void QRGen::ComputeEC() {
    RSEncode(codewords_.data(), DATA_CODEWORDS,
             codewords_.data() + DATA_CODEWORDS, EC_CODEWORDS);
}

void QRGen::PlaceBits() {
    // Place data+EC bits in the matrix following the QR zigzag pattern.
    // Two columns at a time, right to left, alternating upward/downward.

    // Build bit stream from codewords
    std::vector<bool> bits;
    for (int i = 0; i < TOTAL_CODEWORDS; i++) {
        for (int b = 7; b >= 0; b--) {
            bits.push_back((codewords_[i] >> b) & 1);
        }
    }

    int bit_idx = 0;

    // Column pairs, right to left. Skip column 6 (timing pattern).
    int col = SIZE - 1;
    bool upward = true;

    while (col >= 0) {
        if (col == 6) col--;  // skip timing column

        for (int row_step = 0; row_step < SIZE; row_step++) {
            int row = upward ? (SIZE - 1 - row_step) : row_step;

            // Right column of pair, then left column
            for (int dc = 0; dc <= 1; dc++) {
                int c = col - dc;
                if (c < 0) continue;
                if (placed_[row][c]) continue;

                if (bit_idx < static_cast<int>(bits.size())) {
                    matrix_[row][c] = bits[bit_idx];
                    bit_idx++;
                }
                placed_[row][c] = true;
            }
        }

        upward = !upward;
        col -= 2;
    }
}

void QRGen::ApplyMask(int mask_pattern) {
    for (int r = 0; r < SIZE; r++) {
        for (int c = 0; c < SIZE; c++) {
            if (IsFixedModule(r, c)) continue;
            if (IsFormatModule(r, c)) continue;

            bool flip = false;
            switch (mask_pattern) {
                case 0: flip = ((r + c) % 2 == 0); break;
                case 1: flip = (r % 2 == 0); break;
                case 2: flip = (c % 3 == 0); break;
                case 3: flip = ((r + c) % 3 == 0); break;
                case 4: flip = ((r / 2 + c / 3) % 2 == 0); break;
                case 5: flip = ((r * c) % 2 + (r * c) % 3 == 0); break;
                case 6: flip = (((r * c) % 2 + (r * c) % 3) % 2 == 0); break;
                case 7: flip = (((r + c) % 2 + (r * c) % 3) % 2 == 0); break;
            }
            if (flip) matrix_[r][c] = !matrix_[r][c];
        }
    }
}

void QRGen::ComputeFormatInfo(int mask_pattern) {
    // Format info: 2 bits EC level + 3 bits mask → 5 data bits → BCH(15,5) → XOR mask
    // EC level H = 10 binary
    int format_data = (0b10 << 3) | mask_pattern;

    // BCH(15,5) encoding with generator 10100110111 (0x537)
    int bch = format_data << 10;
    int gen = 0x537;
    for (int i = 4; i >= 0; i--) {
        if (bch & (1 << (i + 10))) {
            bch ^= gen << i;
        }
    }
    int format_bits = (format_data << 10) | bch;
    format_bits ^= 0x5412;  // XOR mask: 101010000010010

    // Place format info at two locations
    // First copy: around top-left finder
    // Bits 0-7 along row 8 (cols 0-5, 7, 8)
    // Bits 8-14 along col 8 (rows 7, 5-0)
    static const int row8_cols[] = {0, 1, 2, 3, 4, 5, 7, 8};
    static const int col8_rows[] = {7, 5, 4, 3, 2, 1, 0};

    for (int i = 0; i < 8; i++) {
        matrix_[8][row8_cols[i]] = (format_bits >> (14 - i)) & 1;
    }
    for (int i = 0; i < 7; i++) {
        matrix_[col8_rows[i]][8] = (format_bits >> (14 - (i + 8))) & 1;
    }

    // Second copy: bottom-left col 8 (rows 20-14) + top-right row 8 (cols 13-20)
    for (int i = 0; i < 7; i++) {
        matrix_[SIZE - 1 - i][8] = (format_bits >> (14 - i)) & 1;
    }
    for (int i = 0; i < 8; i++) {
        matrix_[8][SIZE - 8 + i] = (format_bits >> (14 - (i + 7))) & 1;
    }
}

int QRGen::EvaluateMask() const {
    int penalty = 0;

    // Rule 1: Runs of 5+ same-color modules in a row/column
    for (int r = 0; r < SIZE; r++) {
        int run = 1;
        for (int c = 1; c < SIZE; c++) {
            if (matrix_[r][c] == matrix_[r][c-1]) {
                run++;
            } else {
                if (run >= 5) penalty += run - 2;
                run = 1;
            }
        }
        if (run >= 5) penalty += run - 2;
    }
    for (int c = 0; c < SIZE; c++) {
        int run = 1;
        for (int r = 1; r < SIZE; r++) {
            if (matrix_[r][c] == matrix_[r-1][c]) {
                run++;
            } else {
                if (run >= 5) penalty += run - 2;
                run = 1;
            }
        }
        if (run >= 5) penalty += run - 2;
    }

    // Rule 2: 2x2 blocks of same color
    for (int r = 0; r < SIZE - 1; r++) {
        for (int c = 0; c < SIZE - 1; c++) {
            bool v = matrix_[r][c];
            if (matrix_[r][c+1] == v && matrix_[r+1][c] == v && matrix_[r+1][c+1] == v) {
                penalty += 3;
            }
        }
    }

    // Rule 3: Finder-like patterns (1:1:3:1:1 in rows/cols)
    // Simplified: look for 10111010000 or 00001011101
    auto checkPattern = [](const bool* modules, int len) {
        int p = 0;
        static const bool pat1[] = {true,false,true,true,true,false,true,false,false,false,false};
        static const bool pat2[] = {false,false,false,false,true,false,true,true,true,false,true};
        for (int i = 0; i <= len - 11; i++) {
            bool match1 = true, match2 = true;
            for (int j = 0; j < 11; j++) {
                if (modules[i+j] != pat1[j]) match1 = false;
                if (modules[i+j] != pat2[j]) match2 = false;
            }
            if (match1) p += 40;
            if (match2) p += 40;
        }
        return p;
    };
    for (int r = 0; r < SIZE; r++) {
        bool row_data[SIZE];
        for (int c = 0; c < SIZE; c++) row_data[c] = matrix_[r][c];
        penalty += checkPattern(row_data, SIZE);
    }
    for (int c = 0; c < SIZE; c++) {
        bool col_data[SIZE];
        for (int r = 0; r < SIZE; r++) col_data[r] = matrix_[r][c];
        penalty += checkPattern(col_data, SIZE);
    }

    // Rule 4: Proportion of dark modules
    int dark = 0;
    for (int r = 0; r < SIZE; r++)
        for (int c = 0; c < SIZE; c++)
            if (matrix_[r][c]) dark++;
    int total = SIZE * SIZE;
    int pct = (dark * 100) / total;
    int prev5 = (pct / 5) * 5;
    int next5 = prev5 + 5;
    int dev1 = std::abs(prev5 - 50) / 5;
    int dev2 = std::abs(next5 - 50) / 5;
    penalty += std::min(dev1, dev2) * 10;

    return penalty;
}

} // namespace YipOS
