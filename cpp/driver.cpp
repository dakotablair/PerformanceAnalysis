// #include "chimbuko/AD.hpp"
#include "chimbuko/chimbuko.hpp"
#include <chrono>

using namespace chimbuko;
using namespace std::chrono;

// input argument
// - engineType (for BP, + data_dir)
// - output_dir (for now) to dump

// soon later
// - inputFile prefix (i.e. tau-metrics)
// - sigma
// - ps server name 

int main(int argc, char ** argv)
{
    MPI_Init(&argc, &argv);

    int world_rank, world_size;
    MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);
    MPI_Comm_size(MPI_COMM_WORLD, &world_size);    

    try 
    {
        // -----------------------------------------------------------------------
        // parser command line arguments
        // -----------------------------------------------------------------------
        std::string engineType = argv[1]; // BPFile or SST
        std::string data_dir = argv[2]; // *.bp location
        std::string inputFile = "tau-metrics-" + std::to_string(world_rank) + ".bp";
        std::string output_dir = argv[3]; //output directory
        std::string addr;
#ifdef _USE_ZMQNET
        if (argc == 5)
            addr = std::string(argv[4]); // (e.g. "tcp://hostname:5559")
#endif

        if (world_rank == 0) {
        std::cout << "\n" 
                << "rank       : " << world_rank << "\n"
                << "Engine     : " << engineType << "\n"
                << "BP in dir  : " << data_dir << "\n"
                << "BP file    : " << inputFile << "\n"
                << "BP out dir : " << output_dir 
#ifdef _USE_ZMQNET
                << "\nPS Addr    : " << addr
#endif
                << std::endl;
        }

        double sigma = 6.0;

        // -----------------------------------------------------------------------
        // AD module variables
        // -----------------------------------------------------------------------
        Chimbuko driver;

        // -----------------------------------------------------------------------
        // Measurement variables
        // -----------------------------------------------------------------------
        unsigned long total_frames = 0, frames = 0;
        unsigned long total_n_outliers = 0, n_outliers = 0;
        unsigned long total_processing_time = 0, processing_time = 0;
        unsigned long long n_func_events = 0, n_comm_events = 0;
        unsigned long long total_n_func_events = 0, total_n_comm_events = 0; 
        high_resolution_clock::time_point t1, t2;


        // -----------------------------------------------------------------------
        // Init. AD module
        // -----------------------------------------------------------------------
        // First, init io to make sure file (or connection) handler
        driver.init_io(world_rank, output_dir,
            {{"rank", world_rank}, {"algorithm", 0}, {"nparam", 1}, {"winsz", 0}},
            IOMode::Offline);

        // Second, init parser because it will hold shared memory with event and outlier object
        // also, process will be blocked at this line until it finds writer (in SST mode)
        driver.init_parser(data_dir, inputFile, engineType);

        // Thrid, init event and outlier objects
        driver.init_event();
        driver.init_outlier(world_rank, sigma, addr);

        // -----------------------------------------------------------------------
        // Start analysis
        // -----------------------------------------------------------------------
        if (world_rank == 0) {
            std::cout << "rank: " << world_rank 
                    << " analysis start " << (driver.use_ps() ? "with": "without") 
                    << " pserver" << std::endl;
        }

        t1 = high_resolution_clock::now();
        driver.run(world_rank, n_func_events, n_comm_events, n_outliers, frames);
        t2 = high_resolution_clock::now();
        
        if (world_rank == 0) {
            std::cout << "rank: " << world_rank << " analysis done!\n";
            driver.show_status(true);
        }

        // -----------------------------------------------------------------------
        // Average analysis time and total number of outliers
        // -----------------------------------------------------------------------
        MPI_Barrier(MPI_COMM_WORLD);
        processing_time = duration_cast<milliseconds>(t2 - t1).count();

        {
            const unsigned long local_measures[] = {processing_time, n_outliers, frames};
            unsigned long global_measures[] = {0, 0, 0};
            MPI_Reduce(
                local_measures, global_measures, 3, MPI_UNSIGNED_LONG,
                MPI_SUM, 0, MPI_COMM_WORLD
            );
            total_processing_time = global_measures[0];
            total_n_outliers = global_measures[1];
            total_frames = global_measures[2];
        }
        {
            const unsigned long long local_measures[] = {n_func_events, n_comm_events};
            unsigned long long global_measures[] = {0, 0};
            MPI_Reduce(
                local_measures, global_measures, 2, MPI_UNSIGNED_LONG_LONG,
                MPI_SUM, 0, MPI_COMM_WORLD
            );
            total_n_func_events = global_measures[0];
            total_n_comm_events = global_measures[1];
        }

        
        if (world_rank == 0) {
            std::cout << "\n"
                << "Avg. num. frames     : " << (double)total_frames/(double)world_size << "\n"
                << "Avg. processing time : " << (double)total_processing_time/(double)world_size << " msec\n"
                << "Total num. outliers  : " << total_n_outliers << "\n"
                << "Total func events    : " << total_n_func_events << "\n"
                << "Total comm events    : " << total_n_comm_events << "\n"
                << "Total events         : " << total_n_func_events + total_n_comm_events
                << std::endl;
        }

        // -----------------------------------------------------------------------
        // Finalize
        // -----------------------------------------------------------------------
        driver.finalize();
    }
    catch (std::invalid_argument &e)
    {
        std::cout << e.what() << std::endl;
        //todo: usages()
    }
    catch (std::ios_base::failure &e)
    {
        std::cout << "I/O base exception caught\n";
        std::cout << e.what() << std::endl;
    }
    catch (std::exception &e)
    {
        std::cout << "Exception caught\n";
        std::cout << e.what() << std::endl;
    }

    MPI_Finalize();
    return 0;
}
