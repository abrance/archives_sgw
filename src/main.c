
// main.c

extern void init_or_die(int argc, char **argv);
extern void run_events_loop(int thread_id);

int main(int argc, char *argv[])
{
    init_or_die(argc, argv);
    run_events_loop(0); // main thread_id=0
    return -1;
}
