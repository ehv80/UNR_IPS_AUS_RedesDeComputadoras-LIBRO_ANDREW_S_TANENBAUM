#define LENGTH 16			/* # bytes in data block or key */
#define NROWS 4				/* number of rows in state */
#define NCOLS 4				/* number of columns in state */
#define ROUNDS 10			/* number of iterations */
typedef unsigned char byte;		/* unsigned 8-bit integer */

rijndael(byte plaintext[LENGTH], byte ciphertext[LENGTH], byte key[LENGTH])
{
  int r;				/* loop index */
  byte state[NROWS][NCOLS];		/* current state */
  struct {byte k[NROWS][NCOLS];} rk[ROUNDS + 1];	/* round keys */
  expand_key(key, rk);	/* construct the round keys */
  copy_plaintext_to_state(state, plaintext);	/* init current state */
  xor_roundkey_into_state(state, rk[0]);	/* XOR key into state */
  for (r = 1; r <= ROUNDS; r++) {
        substitute(state);			/* apply S-box to each byte */
        rotate_rows(state);			/* rotate row i by i bytes */
        if (r < ROUNDS) mix_columns(state);	/* mix function */
        xor_roundkey_into_state(state, rk[r]);	/* XOR key into state */
  }
  copy_state_to_ciphertext(ciphertext, state);	/* return result */
}

