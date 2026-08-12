// ISCAS-85 c17 benchmark, expressed with built-in SV gate primitives.
module c17(input n1, input n2, input n3, input n6, input n7, output n22, output n23);
  wire n10, n11, n16, n19;
  nand(n10, n1, n3);
  nand(n11, n3, n6);
  nand(n16, n2, n11);
  nand(n19, n11, n7);
  nand(n22, n10, n16);
  nand(n23, n16, n19);
endmodule
