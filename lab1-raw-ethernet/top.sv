module top (
    input  logic       clk,
    input  logic       rst_n,

    // Pipeline Node A -> Node B (tap0 -> tap1)
    input  logic [7:0] a_data_in,
    input  logic       a_valid_in,
    output logic [7:0] b_data_out,
    output logic       b_valid_out,

    // Pipeline Node B -> Node A (tap1 -> tap0)
    input  logic [7:0] b_data_in,
    input  logic       b_valid_in,
    output logic [7:0] a_data_out,
    output logic       a_valid_out
);

    // Tubería A -> B
    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            b_data_out  <= 8'h00;
            b_valid_out <= 1'b0;
        end else begin
            b_data_out  <= a_data_in;
            b_valid_out <= a_valid_in;
        end
    end

    // Tubería B -> A
    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            a_data_out  <= 8'h00;
            a_valid_out <= 1'b0;
        end else begin
            a_data_out  <= b_data_in;
            a_valid_out <= b_valid_in;
        end
    end

endmodule
