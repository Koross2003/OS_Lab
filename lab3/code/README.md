## Code

clock中是未实现lru的版本,可以正常通过make grade
lru中是实现lru的版本,由于在实现lru时添加了struct Page中的变量(后来调试才发现是这个的原因),导致make grade无法正常通过(怀疑make grade可能是用了Page长度的常量值去判定),不过make qemu可以通过自己设计的断言用例