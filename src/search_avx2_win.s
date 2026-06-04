	.file	"search_avx2.c"
	.text
	.p2align 4
	.globl	search_avx2_find
	.def	search_avx2_find;	.scl	2;	.type	32;	.endef
	.seh_proc	search_avx2_find
search_avx2_find:
	pushq	%rdi
	.seh_pushreg	%rdi
	pushq	%rsi
	.seh_pushreg	%rsi
	pushq	%rbx
	.seh_pushreg	%rbx
	.seh_endprologue
	testq	%rdx, %rdx
	movq	%rcx, %r11
	movq	%rdx, %rbx
	je	.L13
	testq	%rcx, %rcx
	je	.L14
	vmovd	%r8d, %xmm1
	movq	%rdx, %rcx
	xorl	%edx, %edx
	vpbroadcastd	%xmm1, %ymm1
	jmp	.L5
	.p2align 4,,10
	.p2align 3
.L8:
	leaq	-8(%rcx), %r10
	shrq	%rax
	addq	%rdx, %rax
	andq	$-8, %rax
	cmpq	%rdx, %rax
	cmovb	%rdx, %rax
	leaq	8(%rax), %rdi
	cmpq	%rdi, %rcx
	movl	$8, %edi
	cmovb	%r10, %rax
	vmovdqu	(%r11,%rax,4), %ymm0
	vpcmpgtd	%ymm1, %ymm0, %ymm0
	vmovmskps	%ymm0, %r10d
	popcntl	%r10d, %r10d
	subl	%r10d, %edi
	jne	.L25
	movq	%rax, %rcx
.L5:
	movq	%rcx, %rax
	subq	%rdx, %rax
	cmpq	$7, %rax
	ja	.L8
	jmp	.L10
	.p2align 5
	.p2align 4,,10
	.p2align 3
.L11:
	movq	%rcx, %rax
	subq	%rdx, %rax
	shrq	%rax
	addq	%rdx, %rax
	cmpl	%r8d, (%r11,%rax,4)
	jl	.L26
	movq	%rax, %rcx
.L10:
	cmpq	%rcx, %rdx
	jb	.L11
	cmpq	%rbx, %rdx
	jnb	.L20
	cmpl	%r8d, (%r11,%rdx,4)
	jne	.L20
	testq	%r9, %r9
	je	.L12
	movq	%rdx, (%r9)
.L12:
	xorl	%eax, %eax
	vzeroupper
.L1:
	popq	%rbx
	popq	%rsi
	popq	%rdi
	ret
	.p2align 4,,10
	.p2align 3
.L26:
	leaq	1(%rax), %rdx
	jmp	.L10
	.p2align 4,,10
	.p2align 3
.L25:
	movslq	%edi, %rdi
	addq	%rax, %rdi
	cmpl	%r8d, -4(%r11,%rdi,4)
	jl	.L16
	cmpq	%rcx, %rdi
	je	.L10
	movq	%rdi, %rcx
	jmp	.L5
	.p2align 4,,10
	.p2align 3
.L16:
	movq	%rdi, %rdx
	jmp	.L5
.L20:
	movl	$-1, %eax
	vzeroupper
	jmp	.L1
.L13:
	movl	$-1, %eax
	jmp	.L1
.L14:
	movl	$-4, %eax
	jmp	.L1
	.seh_endproc
	.ident	"GCC: (x86_64-win32-seh-rev0, Built by MinGW-Builds project) 15.2.0"
