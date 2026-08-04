// Stream fixture: producer (valid/ready) -> 4-deep FIFO -> consumer
module stream_top (
    input  wire        clk,
    input  wire        reset,
    input  wire        in_valid,
    output wire        in_ready,
    input  wire [7:0]  in_data,
    output wire        out_valid,
    input  wire        out_ready,
    output wire [7:0]  out_data
);
    reg [7:0] fifo [0:3];
    reg [1:0] head, tail;
    reg [2:0] count;
    wire full  = (count == 4);
    wire empty = (count == 0);
    assign in_ready  = !full;
    assign out_valid = !empty;
    assign out_data  = fifo[head];
    always @(posedge clk) begin
        if (reset) begin
            head <= 0; tail <= 0; count <= 0;
        end else begin
            if (in_valid && in_ready) begin
                fifo[tail] <= in_data;
                tail <= tail + 1;
                count <= count + 1;
            end
            if (out_valid && out_ready) begin
                head <= head + 1;
                count <= count - 1;
            end
        end
    end
endmodule
