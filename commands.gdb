b proc.c:433
c
p cpus[$tp]->proc->name
delete
b *0x800029f0
c
p cpus[$tp]->proc->name
da                            