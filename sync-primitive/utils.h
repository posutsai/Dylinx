int factorial(int k) {
	int out = 1;
	for (; k >= 1; k--)
		out *= k;
	return out;
}

int n_comb_m(int n, int m) {
	assert(n >= m);
	return factorial(n) / (factorial(m) * factorial(n - m));
}
void comb(int m, int n, unsigned char *c) {
		int i;
		for (i = 0; i < n; i++) c[i] = n - i;

		while (1) {
				for (i = n; i--;)
						printf("%d%c", c[i], i ? ' ': '\n');

				/* this check is not strictly necessary, but if m is not close to n,
				 * 		   it makes the whole thing quite a bit faster */
				i = 0;
				if (c[i]++ < m) continue;

				for (; c[i] >= m - i;) if (++i >= n) return;
				for (c[i]++; i; i--) c[i-1] = c[i] + 1;
		}
}
