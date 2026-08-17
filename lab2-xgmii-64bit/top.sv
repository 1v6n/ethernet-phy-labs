// =========================================================================
// Debug State Definitions & Waveform Mapping (GTKWave / VCD)
// =========================================================================
// Value | State Name   | XGMII Bytes / Lanes Description
// ------+--------------+---------------------------------------------------
//  0    | ST_IDLE      | Transmitting Control Idles (/I/ = 0x07)
//  1    | ST_PREAMBLE  | Start Control (/S/ = 0xFB) + Preamble/SFD bytes
//  2    | ST_DST_MAC   | Destination MAC Address (6 Bytes)
//  3    | ST_SRC_MAC   | Source MAC Address (6 Bytes)
//  4    | ST_ETHERTYPE | EtherType / Length Field (2 Bytes)
//  5    | ST_PAYLOAD   | Network Packet Payload Data
//  6    | ST_FCS       | 32-bit Frame Check Sequence (CRC-32)
//  7    | ST_TERMINATE | Terminate Control (/T/ = 0xFD) + Trailing Idles
// =========================================================================

module top (
    input  logic        clk,
    input  logic        rst_n,

    // Node A -> Node B XGMII Pipeline
    input  logic [63:0] a_txd,
    input  logic [7:0]  a_txc,
    output logic [63:0] b_rxd,
    output logic [7:0]  b_rxc,

    // Node A -> Node B Latched Packet Fields & Debug
    output logic [47:0] mac_dst_a,
    output logic [47:0] mac_src_a,
    output logic [15:0] ethertype_a,
    output logic [31:0] fcs_a,
    output logic [3:0]  debug_state_a,

    // Node B -> Node A XGMII Pipeline
    input  logic [63:0] b_txd,
    input  logic [7:0]  b_txc,
    output logic [63:0] a_rxd,
    output logic [7:0]  a_rxc,

    // Node B -> Node A Latched Packet Fields & Debug
    output logic [47:0] mac_dst_b,
    output logic [47:0] mac_src_b,
    output logic [15:0] ethertype_b,
    output logic [31:0] fcs_b,
    output logic [3:0]  debug_state_b
);

    typedef enum logic [3:0] {
        ST_IDLE      = 4'd0,
        ST_PREAMBLE  = 4'd1,
        ST_DST_MAC   = 4'd2,
        ST_SRC_MAC   = 4'd3,
        ST_ETHERTYPE = 4'd4,
        ST_PAYLOAD   = 4'd5,
        ST_FCS       = 4'd6,
        ST_TERMINATE = 4'd7
    } state_t;

    // -------------------------------------------------------------------------
    // Node A -> Node B Pipeline & Field Extraction Logic
    // -------------------------------------------------------------------------
    logic [15:0]  byte_cnt_a;
    logic [63:0]  prev_a_txd;
    logic [127:0] win_a;

    assign win_a = {a_txd, prev_a_txd};

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            b_rxd         <= 64'h0707070707070707;
            b_rxc         <= 8'hFF;
            debug_state_a <= ST_IDLE;
            byte_cnt_a    <= '0;
            prev_a_txd    <= 64'h0707070707070707;

            mac_dst_a     <= '0;
            mac_src_a     <= '0;
            ethertype_a   <= '0;
            fcs_a         <= '0;
        end else begin
            b_rxd      <= a_txd;
            b_rxc      <= a_txc;
            prev_a_txd <= a_txd;

            // Start of Frame (/S/ on Lane 0): Reset latched header values
            if (a_txc == 8'h01 && a_txd[7:0] == 8'hFB) begin
                debug_state_a <= ST_PREAMBLE;
                byte_cnt_a    <= '0;
                mac_dst_a     <= '0;
                mac_src_a     <= '0;
                ethertype_a   <= '0;
                fcs_a         <= '0;
            end 
            // Data Words Processing
            else if (a_txc == 8'h00) begin
                byte_cnt_a <= byte_cnt_a + 16'd8;

                if (byte_cnt_a == 16'd0) begin
                    debug_state_a   <= ST_DST_MAC;
                    mac_dst_a       <= a_txd[47:0];        // Lanes 0..5
                    mac_src_a[15:0] <= a_txd[63:48];       // Lanes 6..7 (First 2B of Src MAC)
                end else if (byte_cnt_a == 16'd8) begin
                    debug_state_a    <= ST_SRC_MAC;
                    mac_src_a[47:16] <= a_txd[31:0];       // Lanes 0..3 (Last 4B of Src MAC)
                    ethertype_a      <= a_txd[47:32];      // Lanes 4..5
                end else if (byte_cnt_a == 16'd16) begin
                    debug_state_a <= ST_ETHERTYPE;
                end else begin
                    debug_state_a <= ST_PAYLOAD;
                end
            end 
            // Control/Termination Cycle
            else if (a_txc != 8'h00 && a_txc != 8'hFF) begin
                if ((a_txc & 8'hFE) != 8'h00) begin
                    debug_state_a <= ST_FCS;
                end else begin
                    debug_state_a <= ST_TERMINATE;
                end

                // Clean 8-bit slice comparison to identify /T/ control byte (0xFD)
                for (int lane = 0; lane < 8; lane++) begin
                    if (a_txc[lane] && (a_txd[lane*8 +: 8] == 8'hFD)) begin
                        fcs_a <= win_a[(32 + lane * 8) +: 32];
                    end
                end
            end else begin
                debug_state_a <= ST_IDLE;
                byte_cnt_a    <= '0;
            end
        end
    end

    // -------------------------------------------------------------------------
    // Node B -> Node A Pipeline & Field Extraction Logic
    // -------------------------------------------------------------------------
    logic [15:0]  byte_cnt_b;
    logic [63:0]  prev_b_txd;
    logic [127:0] win_b;

    assign win_b = {b_txd, prev_b_txd};

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            a_rxd         <= 64'h0707070707070707;
            a_rxc         <= 8'hFF;
            debug_state_b <= ST_IDLE;
            byte_cnt_b    <= '0;
            prev_b_txd    <= 64'h0707070707070707;

            mac_dst_b     <= '0;
            mac_src_b     <= '0;
            ethertype_b   <= '0;
            fcs_b         <= '0;
        end else begin
            a_rxd      <= b_txd;
            a_rxc      <= b_txc;
            prev_b_txd <= b_txd;

            if (b_txc == 8'h01 && b_txd[7:0] == 8'hFB) begin
                debug_state_b <= ST_PREAMBLE;
                byte_cnt_b    <= '0;
                mac_dst_b     <= '0;
                mac_src_b     <= '0;
                ethertype_b   <= '0;
                fcs_b         <= '0;
            end else if (b_txc == 8'h00) begin
                byte_cnt_b <= byte_cnt_b + 16'd8;

                if (byte_cnt_b == 16'd0) begin
                    debug_state_b   <= ST_DST_MAC;
                    mac_dst_b       <= b_txd[47:0];
                    mac_src_b[15:0] <= b_txd[63:48];
                end else if (byte_cnt_b == 16'd8) begin
                    debug_state_b    <= ST_SRC_MAC;
                    mac_src_b[47:16] <= b_txd[31:0];
                    ethertype_b      <= b_txd[47:32];
                end else if (byte_cnt_b == 16'd16) begin
                    debug_state_b <= ST_ETHERTYPE;
                end else begin
                    debug_state_b <= ST_PAYLOAD;
                end
            end else if (b_txc != 8'h00 && b_txc != 8'hFF) begin
                if ((b_txc & 8'hFE) != 8'h00) begin
                    debug_state_b <= ST_FCS;
                end else begin
                    debug_state_b <= ST_TERMINATE;
                end

                for (int lane = 0; lane < 8; lane++) begin
                    if (b_txc[lane] && (b_txd[lane*8 +: 8] == 8'hFD)) begin
                        fcs_b <= win_b[(32 + lane * 8) +: 32];
                    end
                end
            end else begin
                debug_state_b <= ST_IDLE;
                byte_cnt_b    <= '0;
            end
        end
    end

endmodule
