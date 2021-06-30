struct abc {
    int n;
};
struct xyz {
    int a;
    int b;
    int c;
    struct abc x;
} aaa = (struct xyz){};
struct xyz foo() {
    struct xyz bar;
    bar = (struct xyz){ .a = 99, .c = 100 };
    return (struct xyz){ .x = (struct abc){ .n = 3}};
}
