// ============================================================================
// File: top.sv
// Description: Single-file Back-to-Back Ethernet MAC Layer (MAC A <-> MAC B)
// ============================================================================

module top #(
    parameter bit ENABLE_DEBUG = 1'b0 // Set to 1 for detailed terminal logging
) (
    input  logic        clk,
    input  logic        rst_n,

    // Host A Streaming Interface
    input  logic [63:0] a_tx_data,
    input  logic [7:0]  a_tx_keep,
    input  logic        a_tx_valid,
    input  logic        a_tx_last,
    output logic        a_tx_ready,

    output logic [63:0] a_rx_data,
    output logic [7:0]  a_rx_keep,
    output logic        a_rx_valid,
    output logic        a_rx_last,

    // Host B Streaming Interface
    input  logic [63:0] b_tx_data,
    input  logic [7:0]  b_tx_keep,
    input  logic        b_tx_valid,
    input  logic        b_tx_last,
    output logic        b_tx_ready,

    output logic [63:0] b_rx_data,
    output logic [7:0]  b_rx_keep,
    output logic        b_rx_valid,
    output logic        b_rx_last
);

    // Direct Back-to-Back CGMII Interconnects
    logic [63:0] cgmii_a2b_txd, cgmii_b2a_txd;
    logic [7:0]  cgmii_a2b_txc, cgmii_b2a_txc;

    // MAC Instance A
    mac_tx #(.ENABLE_DEBUG(ENABLE_DEBUG)) mac_tx_a (
        .clk(clk), .rst_n(rst_n),
        .host_tx_data(a_tx_data), .host_tx_keep(a_tx_keep),
        .host_tx_valid(a_tx_valid), .host_tx_last(a_tx_last),
        .host_tx_ready(a_tx_ready),
        .cgmii_txd(cgmii_a2b_txd), .cgmii_txc(cgmii_a2b_txc)
    );

    mac_rx #(.ENABLE_DEBUG(ENABLE_DEBUG)) mac_rx_a (
        .clk(clk), .rst_n(rst_n),
        .cgmii_rxd(cgmii_b2a_txd), .cgmii_rxc(cgmii_b2a_txc),
        .host_rx_data(a_rx_data), .host_rx_keep(a_rx_keep),
        .host_rx_valid(a_rx_valid), .host_rx_last(a_rx_last)
    );

    // MAC Instance B
    mac_tx #(.ENABLE_DEBUG(ENABLE_DEBUG)) mac_tx_b (
        .clk(clk), .rst_n(rst_n),
        .host_tx_data(b_tx_data), .host_tx_keep(b_tx_keep),
        .host_tx_valid(b_tx_valid), .host_tx_last(b_tx_last),
        .host_tx_ready(b_tx_ready),
        .cgmii_txd(cgmii_b2a_txd), .cgmii_txc(cgmii_b2a_txc)
    );

    mac_rx #(.ENABLE_DEBUG(ENABLE_DEBUG)) mac_rx_b (
        .clk(clk), .rst_n(rst_n),
        .cgmii_rxd(cgmii_a2b_txd), .cgmii_rxc(cgmii_a2b_txc),
        .host_rx_data(b_rx_data), .host_rx_keep(b_rx_keep),
        .host_rx_valid(b_rx_valid), .host_rx_last(b_rx_last)
    );

endmodule


// ============================================================================
// MAC Transmitter: Preamble/SFD Prepending, Min-Length Padding, FCS Calculation
// ============================================================================
module mac_tx #(
    parameter bit ENABLE_DEBUG = 1'b0
) (
    input  logic        clk,
    input  logic        rst_n,

    input  logic [63:0] host_tx_data,
    input  logic [7:0]  host_tx_keep,
    input  logic        host_tx_valid,
    input  logic        host_tx_last,
    output logic        host_tx_ready,

    output logic [63:0] cgmii_txd,
    output logic [7:0]  cgmii_txc
);

    // IEEE 802.3 CRC-32 Calculation (Polynomial 0xEDB88320)
    function automatic logic [31:0] crc32_byte(input logic [31:0] crc_in, input logic [7:0] data_byte);
        logic [31:0] crc;
        crc = crc_in ^ {24'b0, data_byte};
        for (int j = 0; j < 8; j++) begin
            if (crc[0])
                crc = (crc >> 1) ^ 32'hEDB88320;
            else
                crc = crc >> 1;
        end
        return crc;
    endfunction

    function automatic int count_bytes(input logic [7:0] keep);
        int c = 0;
        for (int i = 0; i < 8; i++) if (keep[i]) c++;
        return c;
    endfunction

    typedef enum logic [1:0] {ST_IDLE, ST_INGEST, ST_START, ST_DATA} state_t;
    state_t state;

    logic [7:0] pkt_buf [0:2047];
    int         pkt_len;
    int         tx_pos;

    // Public flat registers for Verilator wave inspection
    int tx_frame_cnt /* verilator public_flat */;

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            state         <= ST_IDLE;
            host_tx_ready <= 1'b1;
            cgmii_txd     <= 64'h0707070707070707; // Control Idle (/I/)
            cgmii_txc     <= 8'hFF;
            pkt_len       <= 0;
            tx_pos        <= 0;
            tx_frame_cnt  <= 0;
        end else begin
            case (state)
                ST_IDLE: begin
                    cgmii_txd     <= 64'h0707070707070707;
                    cgmii_txc     <= 8'hFF;
                    host_tx_ready <= 1'b1;

                    if (host_tx_valid && host_tx_ready) begin
                        int b_cnt = count_bytes(host_tx_keep);
                        /* verilator lint_off BLKSEQ */
                        for (int i = 0; i < 8; i++) begin
                            if (host_tx_keep[i]) pkt_buf[i] = host_tx_data[i*8 +: 8];
                        end
                        /* verilator lint_on BLKSEQ */
                        pkt_len <= b_cnt;

                        if (host_tx_last) begin
                            host_tx_ready <= 1'b0;
                            state         <= ST_START;
                        end else begin
                            state         <= ST_INGEST;
                        end
                    end
                end

                ST_INGEST: begin
                    host_tx_ready <= 1'b1;
                    if (host_tx_valid && host_tx_ready) begin
                        int b_cnt = count_bytes(host_tx_keep);
                        /* verilator lint_off BLKSEQ */
                        for (int i = 0; i < 8; i++) begin
                            if (host_tx_keep[i]) pkt_buf[pkt_len + i] = host_tx_data[i*8 +: 8];
                        end
                        /* verilator lint_on BLKSEQ */
                        pkt_len <= pkt_len + b_cnt;

                        if (host_tx_last) begin
                            host_tx_ready <= 1'b0;
                            state         <= ST_START;
                        end
                    end
                end

                ST_START: begin
                    logic [31:0] crc_calc;
                    int final_len;
                    host_tx_ready <= 1'b0;

                    // 1. Minimum Length Padding (60 bytes payload minimum)
                    final_len = pkt_len;
                    if (final_len < 60) begin
                        /* verilator lint_off BLKSEQ */
                        for (int k = final_len; k < 60; k++) begin
                            pkt_buf[k] = 8'h00;
                        end
                        /* verilator lint_on BLKSEQ */
                        final_len = 60;
                    end

                    // 2. Calculate CRC-32 (FCS) over Padded Payload
                    crc_calc = 32'hFFFFFFFF;
                    for (int i = 0; i < final_len; i++) begin
                        crc_calc = crc32_byte(crc_calc, pkt_buf[i]);
                    end
                    crc_calc = ~crc_calc;

                    // 3. Append 4-byte FCS
                    pkt_buf[final_len]     <= crc_calc[7:0];
                    pkt_buf[final_len + 1] <= crc_calc[15:8];
                    pkt_buf[final_len + 2] <= crc_calc[23:16];
                    pkt_buf[final_len + 3] <= crc_calc[31:24];

                    pkt_len <= final_len + 4;

                    // 4. Output Start Delimiter (/S/) + Preamble (0x55) + SFD (0xD5)
                    cgmii_txd    <= 64'hD5555555555555FB;
                    cgmii_txc    <= 8'h01;
                    tx_pos       <= 0;
                    tx_frame_cnt <= tx_frame_cnt + 1;

                    if (ENABLE_DEBUG) begin
                        $display("[MAC_TX DEBUG] [%0t ps] Transmitting Frame #%0d (Raw Bytes: %0d, Padded+FCS: %0d, FCS: 0x%08X)", 
                                 $time, tx_frame_cnt + 1, pkt_len, final_len + 4, crc_calc);
                    end

                    state <= ST_DATA;
                end

                ST_DATA: begin
                    int rem_len = pkt_len - tx_pos;
                    logic [63:0] d_temp;
                    logic [7:0]  c_temp;

                    host_tx_ready <= 1'b0;

                    if (rem_len >= 8) begin
                        for (int b = 0; b < 8; b++) d_temp[b*8 +: 8] = pkt_buf[tx_pos + b];
                        c_temp    = 8'h00;
                        tx_pos    <= tx_pos + 8;
                        cgmii_txd <= d_temp;
                        cgmii_txc <= c_temp;
                    end else if (rem_len == 0) begin
                        cgmii_txd <= 64'h07070707070707FD; // Terminate + Idles
                        cgmii_txc <= 8'hFF;
                        state     <= ST_IDLE;
                    end else begin
                        d_temp = 64'h0707070707070707;
                        c_temp = 8'h00;
                        for (int b = 0; b < 8; b++) begin
                            if (b < rem_len) begin
                                d_temp[b*8 +: 8] = pkt_buf[tx_pos + b];
                            end else if (b == rem_len) begin
                                d_temp[b*8 +: 8] = 8'hFD; // /T/ Terminate
                                c_temp[b]        = 1'b1;
                            end else begin
                                d_temp[b*8 +: 8] = 8'h07; // /I/ Idle
                                c_temp[b]        = 1'b1;
                            end
                        end
                        cgmii_txd <= d_temp;
                        cgmii_txc <= c_temp;
                        state     <= ST_IDLE;
                    end
                end

                default: state <= ST_IDLE;
            endcase
        end
    end
endmodule


// ============================================================================
// MAC Receiver: Delimiter Parse, FCS Verification & Stripping, Output Stream
// ============================================================================
module mac_rx #(
    parameter bit ENABLE_DEBUG = 1'b0
) (
    input  logic        clk,
    input  logic        rst_n,

    input  logic [63:0] cgmii_rxd,
    input  logic [7:0]  cgmii_rxc,

    output logic [63:0] host_rx_data,
    output logic [7:0]  host_rx_keep,
    output logic        host_rx_valid,
    output logic        host_rx_last
);

    function automatic logic [31:0] crc32_byte(input logic [31:0] crc_in, input logic [7:0] data_byte);
        logic [31:0] crc;
        crc = crc_in ^ {24'b0, data_byte};
        for (int j = 0; j < 8; j++) begin
            if (crc[0])
                crc = (crc >> 1) ^ 32'hEDB88320;
            else
                crc = crc >> 1;
        end
        return crc;
    endfunction

    typedef enum logic [1:0] {ST_IDLE, ST_COLLECT, ST_OUTPUT} state_t;
    state_t state;

    logic [7:0] rx_buf [0:2047];
    int         rx_len;
    int         out_pos;

    // Debug signals exposed to Verilator
    int rx_frame_cnt  /* verilator public_flat */;
    int crc_error_cnt /* verilator public_flat */;

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            state         <= ST_IDLE;
            host_rx_data  <= '0;
            host_rx_keep  <= '0;
            host_rx_valid <= 1'b0;
            host_rx_last  <= 1'b0;
            rx_len        <= 0;
            out_pos       <= 0;
            rx_frame_cnt  <= 0;
            crc_error_cnt <= 0;
        end else begin
            case (state)
                ST_IDLE: begin
                    host_rx_valid <= 1'b0;
                    host_rx_last  <= 1'b0;
                    if (cgmii_rxc[0] && (cgmii_rxd[7:0] == 8'hFB)) begin
                        rx_len <= 0;
                        state  <= ST_COLLECT;
                    end
                end

                ST_COLLECT: begin
                    if (cgmii_rxc == 8'h00) begin
                        /* verilator lint_off BLKSEQ */
                        for (int b = 0; b < 8; b++) begin
                            rx_buf[rx_len + b] = cgmii_rxd[b*8 +: 8];
                        end
                        /* verilator lint_on BLKSEQ */
                        rx_len <= rx_len + 8;
                    end else begin
                        int          valid_bytes = 0;
                        int          total_rx;
                        logic [31:0] calc_crc;
                        logic [31:0] rx_crc;
                        logic [7:0]  full_pkt [0:2047];

                        for (int k = 0; k < rx_len; k++) full_pkt[k] = rx_buf[k];

                        /* verilator lint_off BLKSEQ */
                        for (int b = 0; b < 8; b++) begin
                            if (cgmii_rxc[b] == 1'b0) begin
                                full_pkt[rx_len + b] = cgmii_rxd[b*8 +: 8];
                                rx_buf[rx_len + b]   = cgmii_rxd[b*8 +: 8];
                                valid_bytes++;
                            end else begin
                                break;
                            end
                        end
                        /* verilator lint_on BLKSEQ */

                        total_rx = rx_len + valid_bytes;

                        if (total_rx >= 64) begin
                            calc_crc = 32'hFFFFFFFF;
                            for (int i = 0; i < total_rx - 4; i++) begin
                                calc_crc = crc32_byte(calc_crc, full_pkt[i]);
                            end
                            calc_crc = ~calc_crc;

                            rx_crc = {full_pkt[total_rx-1], full_pkt[total_rx-2], full_pkt[total_rx-3], full_pkt[total_rx-4]};

                            if (calc_crc == rx_crc) begin
                                rx_len       <= total_rx - 4; // Strip FCS
                                out_pos      <= 0;
                                rx_frame_cnt <= rx_frame_cnt + 1;
                                state        <= ST_OUTPUT;

                                if (ENABLE_DEBUG) begin
                                    $display("[MAC_RX DEBUG] [%0t ps] FCS Validated Successfully! Stripping 4-byte FCS, forwarding %0d bytes to host.", 
                                             $time, total_rx - 4);
                                end
                            end else begin
                                crc_error_cnt <= crc_error_cnt + 1;
                                if (ENABLE_DEBUG) begin
                                    $display("[MAC_RX ERROR] [%0t ps] FCS Mismatch! Expected 0x%08X, Received 0x%08X", 
                                             $time, calc_crc, rx_crc);
                                end
                                state <= ST_IDLE;
                            end
                        end else begin
                            if (ENABLE_DEBUG) begin
                                $display("[MAC_RX ERROR] [%0t ps] Runt Frame Dropped! Total Bytes: %0d (Minimum required: 64)", 
                                         $time, total_rx);
                            end
                            state <= ST_IDLE;
                        end
                    end
                end

                ST_OUTPUT: begin
                    int rem_rx = rx_len - out_pos;

                    if (rem_rx > 8) begin
                        logic [63:0] d_rx_temp = '0;
                        logic [7:0]  k_rx_temp = 8'hFF;
                        for (int b = 0; b < 8; b++) d_rx_temp[b*8 +: 8] = rx_buf[out_pos + b];
                        host_rx_data  <= d_rx_temp;
                        host_rx_keep  <= k_rx_temp;
                        host_rx_valid <= 1'b1;
                        host_rx_last  <= 1'b0;
                        out_pos       <= out_pos + 8;
                    end else if (rem_rx > 0) begin
                        logic [63:0] d_rx_temp = '0;
                        logic [7:0]  k_rx_temp = '0;
                        for (int b = 0; b < rem_rx; b++) begin
                            d_rx_temp[b*8 +: 8] = rx_buf[out_pos + b];
                            k_rx_temp[b]        = 1'b1;
                        end
                        host_rx_data  <= d_rx_temp;
                        host_rx_keep  <= k_rx_temp;
                        host_rx_valid <= 1'b1;
                        host_rx_last  <= 1'b1;
                        out_pos       <= rx_len;
                        state         <= ST_IDLE;
                    end else begin
                        host_rx_valid <= 1'b0;
                        host_rx_last  <= 1'b0;
                        state         <= ST_IDLE;
                    end
                end

                default: state <= ST_IDLE;
            endcase
        end
    end
endmodule
