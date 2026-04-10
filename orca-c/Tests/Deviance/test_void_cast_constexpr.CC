/* Deviance test: cast to void in an integer constant expression.
 * Fix 6 (794ca70) from sheumann-orca-c / ORCA/C 2.2.5.
 * Using (void)expr as a case label must produce an error. */

int main(int argc, char *argv[]) {
        switch (argc) {
                case (void)0: return 1;
        }
        return 0;
}
