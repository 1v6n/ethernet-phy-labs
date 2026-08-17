// ============================================================================
// Module: top.sv
// Description: IEEE 802.3 Clause 49 PCS 64b/66b Encoder, Scrambler, 
//              Descrambler, and Block Lock Finite State Machine.
// ============================================================================

module top (
    input  logic        clk,
    input  logic        rst_n,

    // XGMII Transmit Interface (From MAC / Wrapper)
    input  logic [63:0] xgmii_txd,
    input  logic [7:0]  xgmii_txc,

    // PCS Transmit Signals (Exposing both Unscrambled & Scrambled for Comparison)
    output logic [65:0] unscrambled_tx_block,  // [1:0] Sync Header, [65:2] Raw Payload
    output logic [65:0] scrambled_tx_block,    // [1:0] Sync Header, [65:2] Scrambled Payload

    // PCS Receive Signals (Loopback / Reconstructed)
    output logic [63:0] xgmii_rxd,
    output logic [7:0]  xgmii_rxc,
    output logic        block_lock
);

    // Sync Header Definitions (IEEE 802.3 Clause 49)
    localparam [1:0] SYNC_DATA = 2'b01; // Data Block
    localparam [1:0] SYNC_CTRL = 2'b10; // Control or Mixed Block

    // ------------------------------------------------------------------------
    // 1. 64b/66b Encoder Logic
    // ------------------------------------------------------------------------
    logic [1:0]  sync_header;
    logic [63:0] raw_payload;

    always_comb begin
        if (xgmii_txc == 8'h00) begin
            // Pure Data Block
            sync_header = SYNC_DATA;
            raw_payload = xgmii_txd;
        end else begin
            // Control / Mixed Block
            sync_header = SYNC_CTRL;
            // Simplified Clause 49 Control Block Format (Block Type 0x1E for Control)
            raw_payload = {xgmii_txd[55:0], 8'h1E}; 
        end
    end

    assign unscrambled_tx_block = {raw_payload, sync_header};

    // ------------------------------------------------------------------------
    // 2. Polynomial Scrambler: G(X) = 1 + X^39 + X^58
    // ------------------------------------------------------------------------
    logic [57:0] scram_state;
    logic [63:0] scrambled_payload;

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            scram_state       <= 58'h3FFFFFFFFFFFFFF; // Non-zero LFSR seed
            scrambled_payload <= '0;
        end else begin
            automatic logic [57:0] lfsr = scram_state;
            for (int i = 0; i < 64; i++) begin
                automatic logic bit_in  = raw_payload[i];
                automatic logic bit_out = bit_in ^ lfsr[38] ^ lfsr[57];
                scrambled_payload[i] <= bit_out;
                lfsr = {lfsr[56:0], bit_out};
            end
            scram_state <= lfsr;
        end
    end

    // Sync headers pass through unscrambled in both blocks
    assign scrambled_tx_block = {scrambled_payload, sync_header};

    // ------------------------------------------------------------------------
    // 3. Receiver Sync Header Lock Monitor (Block Lock FSM)
    // ------------------------------------------------------------------------
    logic [6:0] sh_cnt; // 7 bits required to cleanly represent values up to 64

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            sh_cnt     <= '0;
            block_lock <= 1'b0;
        end else begin
            // Valid Sync Header Check (01 or 10)
            if (sync_header == SYNC_DATA || sync_header == SYNC_CTRL) begin
                if (sh_cnt < 7'd64) begin
                    sh_cnt <= sh_cnt + 1'b1;
                end else begin
                    block_lock <= 1'b1;
                end
            end else begin
                // Invalid Sync Header (00 or 11) resets lock
                sh_cnt     <= '0;
                block_lock <= 1'b0;
            end
        end
    end

    // ------------------------------------------------------------------------
    // 4. Descrambler & Loopback Decoder
    // ------------------------------------------------------------------------
    logic [57:0] descram_state;
    logic [63:0] descrambled_payload;

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            descram_state       <= 58'h3FFFFFFFFFFFFFF;
            descrambled_payload <= '0;
        end else begin
            automatic logic [57:0] lfsr = descram_state;
            for (int i = 0; i < 64; i++) begin
                automatic logic bit_in  = scrambled_payload[i];
                automatic logic bit_out = bit_in ^ lfsr[38] ^ lfsr[57];
                descrambled_payload[i] <= bit_out;
                lfsr = {lfsr[56:0], bit_in};
            end
            descram_state <= lfsr;
        end
    end

    // Reconstruct XGMII stream
    assign xgmii_rxd = (sync_header == SYNC_DATA) ? descrambled_payload : xgmii_txd;
    assign xgmii_rxc = xgmii_txc;

endmodule
