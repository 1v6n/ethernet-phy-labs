/* verilator lint_off DECLFILENAME */

// ============================================================================
// Module: top.sv (Dual Channel 64b/66b PCS with Debug Tracing Outputs)
// ============================================================================
module top (
    input  logic        clk,
    input  logic        rst_n,

    // Interface A (Host / tap0)
    input  logic [63:0] a_txd,
    input  logic [7:0]  a_txc,
    output logic [63:0] a_rxd,
    output logic [7:0]  a_rxc,

    // Interface B (ns_b / tap1)
    input  logic [63:0] b_txd,
    input  logic [7:0]  b_txc,
    output logic [63:0] b_rxd,
    output logic [7:0]  b_rxc,

    // Direction A -> B Debug Signals (Viewable in VCD at TOP.top)
    output logic [65:0] a2b_unscrambled_tx_block,
    output logic [65:0] a2b_scrambled_tx_block,
    output logic        a2b_block_lock,

    // Direction B -> A Debug Signals (Viewable in VCD at TOP.top)
    output logic [65:0] b2a_unscrambled_tx_block,
    output logic [65:0] b2a_scrambled_tx_block,
    output logic        b2a_block_lock
);

    // Channel 1: Host (Interface A) ---> ns_b (Interface B)
    pcs_channel pcs_inst_a2b (
        .clk                   (clk),
        .rst_n                 (rst_n),
        .tx_data               (a_txd),
        .tx_ctrl               (a_txc),
        .rx_data               (b_rxd),
        .rx_ctrl               (b_rxc),
        .unscrambled_tx_block  (a2b_unscrambled_tx_block),
        .scrambled_tx_block    (a2b_scrambled_tx_block),
        .block_lock            (a2b_block_lock)
    );

    // Channel 2: ns_b (Interface B) ---> Host (Interface A)
    pcs_channel pcs_inst_b2a (
        .clk                   (clk),
        .rst_n                 (rst_n),
        .tx_data               (b_txd),
        .tx_ctrl               (b_txc),
        .rx_data               (a_rxd),
        .rx_ctrl               (a_rxc),
        .unscrambled_tx_block  (b2a_unscrambled_tx_block),
        .scrambled_tx_block    (b2a_scrambled_tx_block),
        .block_lock            (b2a_block_lock)
    );

endmodule


// ============================================================================
// Internal Module: Single-Direction 64b/66b PCS Channel Pipeline
// ============================================================================
module pcs_channel (
    input  logic        clk,
    input  logic        rst_n,
    input  logic [63:0] tx_data,
    input  logic [7:0]  tx_ctrl,
    output logic [63:0] rx_data,
    output logic [7:0]  rx_ctrl,
    output logic [65:0] unscrambled_tx_block,
    output logic [65:0] scrambled_tx_block,
    output logic        block_lock
);

    logic [65:0] raw_block;

    // 1. Encoder (XGMII -> 66-bit Unscrambled Block)
    pcs_encoder encoder_inst (
        .tx_data    (tx_data),
        .tx_ctrl    (tx_ctrl),
        .raw_block  (raw_block)
    );

    assign unscrambled_tx_block = raw_block;

    // 2. Scrambler G(X) = 1 + X^39 + X^58
    pcs_scrambler scrambler_inst (
        .clk            (clk),
        .rst_n          (rst_n),
        .raw_block      (raw_block),
        .scrambled_block(scrambled_tx_block)
    );

    // 3. Sync Lock Monitor (Evaluates 2-bit Sync Headers)
    pcs_sync_lock sync_lock_inst (
        .clk        (clk),
        .rst_n      (rst_n),
        .sync_header(scrambled_tx_block[1:0]),
        .block_lock (block_lock)
    );

    // 4. Descrambler
    logic [65:0] unscrambled_rx_block;
    pcs_descrambler descrambler_inst (
        .clk              (clk),
        .rst_n            (rst_n),
        .scrambled_block  (scrambled_tx_block),
        .unscrambled_block(unscrambled_rx_block)
    );

    // 5. Decoder (66-bit Block -> XGMII Receive Signals)
    pcs_decoder decoder_inst (
        .unscrambled_block(unscrambled_rx_block),
        .rx_data          (rx_data),
        .rx_ctrl          (rx_ctrl)
    );

endmodule


// ============================================================================
// Submodule: 64b/66b Encoder
// ============================================================================
module pcs_encoder (
    input  logic [63:0] tx_data,
    input  logic [7:0]  tx_ctrl,
    output logic [65:0] raw_block
);

    always_comb begin
        if (tx_ctrl == 8'h00) begin
            // Data Block: Sync Header = 2'b01
            raw_block = {tx_data, 2'b01};
        end else begin
            // Control/Idle Block: Sync Header = 2'b10, Block Format = 0x1E
            raw_block = {{tx_data[55:0], 8'h1E}, 2'b10};
        end
    end

endmodule


// ============================================================================
// Submodule: Self-Synchronizing Scrambler (G(X) = 1 + X^39 + X^58)
// ============================================================================
module pcs_scrambler (
    input  logic        clk,
    input  logic        rst_n,
    input  logic [65:0] raw_block,
    output logic [65:0] scrambled_block
);

    logic [57:0] lfsr;
    logic [63:0] scram_data;

    always_comb begin
        scram_data = '0;
        for (int j = 0; j < 64; j++) begin
            logic bit_39 = (j >= 39) ? scram_data[j-39] : lfsr[57 - (38 - j)];
            logic bit_58 = (j >= 58) ? scram_data[j-58] : lfsr[57 - (57 - j)];
            scram_data[j] = raw_block[j+2] ^ bit_39 ^ bit_58;
        end
    end

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            lfsr <= 58'h3FFFFFFFFFFFFFF;
        end else begin
            lfsr <= scram_data[63:6];
        end
    end

    // Sync header (bits [1:0]) is transmitted transparently (unscrambled)
    assign scrambled_block = {scram_data, raw_block[1:0]};

endmodule


// ============================================================================
// Submodule: Sync Lock / Alignment Monitor
// ============================================================================
module pcs_sync_lock (
    input  logic       clk,
    input  logic       rst_n,
    input  logic [1:0] sync_header,
    output logic       block_lock
);

    logic [6:0] lock_counter;

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            lock_counter <= '0;
            block_lock   <= 1'b0;
        end else begin
            if (sync_header == 2'b01 || sync_header == 2'b10) begin
                if (lock_counter < 7'd64) begin
                    lock_counter <= lock_counter + 1'b1;
                end else begin
                    block_lock <= 1'b1;
                end
            end else begin
                // Invalid Sync Header (2'b00 or 2'b11) resets lock
                lock_counter <= '0;
                block_lock   <= 1'b0;
            end
        end
    end

endmodule


// ============================================================================
// Submodule: Descrambler
// ============================================================================
module pcs_descrambler (
    input  logic        clk,
    input  logic        rst_n,
    input  logic [65:0] scrambled_block,
    output logic [65:0] unscrambled_block
);

    logic [57:0] descram_lfsr;
    logic [63:0] descram_data;

    always_comb begin
        descram_data = '0;
        for (int j = 0; j < 64; j++) begin
            logic bit_39 = (j >= 39) ? scrambled_block[j+2-39] : descram_lfsr[57 - (38 - j)];
            logic bit_58 = (j >= 58) ? scrambled_block[j+2-58] : descram_lfsr[57 - (57 - j)];
            descram_data[j] = scrambled_block[j+2] ^ bit_39 ^ bit_58;
        end
    end

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            descram_lfsr <= '0;
        end else begin
            descram_lfsr <= scrambled_block[65:8];
        end
    end

    assign unscrambled_block = {descram_data, scrambled_block[1:0]};

endmodule


// ============================================================================
// Submodule: 64b/66b Decoder
// ============================================================================
module pcs_decoder (
    input  logic [65:0] unscrambled_block,
    output logic [63:0] rx_data,
    output logic [7:0]  rx_ctrl
);

    always_comb begin
        if (unscrambled_block[1:0] == 2'b01) begin
            // Pure Data Block
            rx_data = unscrambled_block[65:2];
            rx_ctrl = 8'h00;
        end else begin
            // Control/Idle Block
            rx_data = 64'h0707070707070707;
            rx_ctrl = 8'hFF;
        end
    end

endmodule
