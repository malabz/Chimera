/*
 * -----------------------------------------------------------------------------
 * Filename:      ChimeraBuild.cpp
 *
 * Author:        Qinzhong Tian
 *
 * Email:         tianqinzhong@qq.com
 *
 * Created Date:  2024-07-30
 *
 * Last Modified: 2024-08-09
 *
 * Description:
 *  The main program of ChimeraBuild.
 *
 * Version:
 *  1.0
 * -----------------------------------------------------------------------------
 */
#include <ChimeraBuild.hpp>

namespace ChimeraBuild {
	/**
	* Print the build time in a human-readable format.
	*
	* @param milliseconds The build time in milliseconds.
	*/
    void print_build_time(long long milliseconds) {
        // Calculate seconds, minutes, and hours
        long long total_seconds = milliseconds / 1000;
        long long seconds = total_seconds % 60;
        long long total_minutes = total_seconds / 60;
        long long minutes = total_minutes % 60;
        long long hours = total_minutes / 60;

        // Output different formats based on the length of time
        if (hours > 0) {
            std::cout << hours << "h " << minutes << "min " << seconds << "s " << milliseconds % 1000 << "ms" << std::endl;
        }
        else if (minutes > 0) {
            std::cout << minutes << "min " << seconds << "s " << milliseconds % 1000 << "ms" << std::endl;
        }
        else {
            std::cout << seconds << "s " << milliseconds % 1000 << "ms" << std::endl;
        }
    }

	/**
	* Parse the input file and populate the inputFiles and hashCount maps.
	*
	* @param filePath The path to the input file.
	* @param inputFiles The map to store the input files.
	* @param hashCount The map to store the hash count.
	* @param fileInfo The struct to store file information.
	*/
	void parseInputFile(const std::string& filePath, robin_hood::unordered_map<std::string, std::vector<std::string>>& inputFiles, robin_hood::unordered_map<std::string, uint64_t>& hashCount, FileInfo& fileInfo) {
		// Open the input file
		std::ifstream inputFile(filePath);
		if (!inputFile.is_open()) {
			std::cerr << "Failed to open input file: " << filePath << std::endl;
			return;
		}
		std::string line;
		while (std::getline(inputFile, line)) {
			std::istringstream iss(line);
			std::string filePath;
			std::string taxidStr;
			if (!(iss >> filePath >> taxidStr)) {
				std::cerr << "Failed to parse line: " << line << std::endl;
				fileInfo.invalidNum++;
				continue;
			}
			hashCount[taxidStr] = 0;
			inputFiles[taxidStr].push_back(filePath);
			fileInfo.fileNum++;
		}

		// Close the input file
		inputFile.close();
	}

	/**
	* Create or reset the directory specified by the dir parameter.
	*
	* @param dir The directory path.
	* @param config The build configuration.
	*/
	void createOrResetDirectory(const std::string& dir, const BuildConfig& config) {
		try {
			std::filesystem::path directoryPath = dir;

			// Check if the directory exists
			if (std::filesystem::exists(directoryPath)) {
				if (std::filesystem::is_directory(directoryPath)) {
					// If it is a directory, remove it
					std::filesystem::remove_all(directoryPath);
					if (config.verbose) {
						std::cout << "Directory '" << dir << "' existed and was removed." << std::endl;
					}
				}
				else {
					// If it is a file instead of a directory, print an error message and return
					if (config.verbose) {
						std::cerr << "'" << dir << "' exists but is not a directory, can't be replaced." << std::endl;
					}
					return;
				}
			}

			// Create a new directory
			if (std::filesystem::create_directory(directoryPath)) {
				if (config.verbose) {
					std::cout << "Directory '" << dir << "' created successfully." << std::endl;
				}
			}
			else {
				if (config.verbose) {
					std::cerr << "Failed to create directory '" << dir << "'." << std::endl;
				}
			}
		}
		catch (const std::filesystem::filesystem_error& e) {
			if (config.verbose) {
				std::cerr << "Error: " << e.what() << std::endl;
			}
		}
	}

	/**
	 * Adjust the seed value based on the kmer size.
	 *
	 * @param kmer_size The size of the kmer.
	 * @param seed The seed value to adjust (default: 0x8F3F73B5CF1C9ADEULL).
	 * @return The adjusted seed value.
	 */
	inline constexpr static uint64_t adjust_seed(uint8_t const kmer_size, uint64_t const seed = 0x8F3F73B5CF1C9ADEULL) noexcept {
		// Right shift the seed by (64 - 2 * kmer_size) bits
		return seed >> (64u - 2u * kmer_size);
	}

	/**
	 * Count the minimisers for each taxid and its associated files.
	 *
	 * @param config The build configuration.
	 * @param inputFiles The map of input files.
	 * @param hashCount The map to store the hash count.
	 * @param fileInfo The struct to store file information.
	 */
	void minimiser_count(
		BuildConfig& config,
		robin_hood::unordered_map<std::string, std::vector<std::string>>& inputFiles,
		robin_hood::unordered_map<std::string, uint64_t>& hashCount,
		FileInfo& fileInfo)
	{
		auto minimiser_view = seqan3::views::minimiser_hash(
			seqan3::shape{ seqan3::ungapped{ config.kmer_size } },
			seqan3::window_size{ config.window_size },
			seqan3::seed{ adjust_seed(config.kmer_size) });

		std::vector<std::pair<std::string, std::string>> taxid_file_pairs;
		for (auto& [taxid, files] : inputFiles)
		{
			for (auto& file : files)
			{
				taxid_file_pairs.emplace_back(taxid, file);
			}
		}

		std::unordered_map<std::string, std::mutex> file_mutex_map;

		std::mutex hashCount_mutex;
		std::mutex fileInfo_mutex;

#pragma omp parallel
		{
			std::unordered_map<std::string, uint64_t> threadHashCount;
			FileInfo threadFileInfo = {};

#pragma omp for nowait schedule(dynamic)
			for (size_t i = 0; i < taxid_file_pairs.size(); ++i)
			{
				auto& [taxid, file] = taxid_file_pairs[i];

				seqan3::sequence_file_input<raptor::dna4_traits, seqan3::fields< seqan3::field::id, seqan3::field::seq >> fin{ file };
				robin_hood::unordered_set<uint64_t> hashes;

				for (auto const& [header, seq] : fin)
				{
					if (seq.size() < config.min_length)
					{
						threadFileInfo.skippedNum++;
						continue;
					}
					threadFileInfo.sequenceNum++;
					threadFileInfo.bpLength += seq.size();

					const auto minihash = seq | minimiser_view | std::views::common;
					hashes.insert(minihash.begin(), minihash.end());
				}

				threadHashCount[taxid] += hashes.size();

				{
					std::lock_guard<std::mutex> lock(file_mutex_map[taxid]);
					std::ofstream ofile("tmp/" + taxid + ".mini", std::ios::binary | std::ios::app);
					if (!ofile.is_open())
					{
						std::cerr << "Unable to open the minimize file:" << taxid << ".mini" << std::endl;
						continue;
					}
					for (auto hash : hashes)
					{
						ofile.write(reinterpret_cast<char*>(&hash), sizeof(hash));
					}
				}
			}

			{
				std::lock_guard<std::mutex> lock(hashCount_mutex);
				for (const auto& [taxid, count] : threadHashCount)
				{
					hashCount[taxid] += count;
				}
			}
			{
				std::lock_guard<std::mutex> lock(fileInfo_mutex);
				fileInfo.skippedNum += threadFileInfo.skippedNum;
				fileInfo.sequenceNum += threadFileInfo.sequenceNum;
				fileInfo.bpLength += threadFileInfo.bpLength;
			}
		}
	}
    /**
    * Get the maximum value from the hashCount map.
    *
    * @param hashCount The map containing the values.
    * @return The maximum value.
    */
	uint64_t getMaxValue(const robin_hood::unordered_map<std::string, uint64_t>& hashCount) {
		uint64_t maxValue = 0;
		for (const auto& kv : hashCount) {
			if (kv.second > maxValue) {
				maxValue = kv.second;
			}
		}
		return maxValue;
	}

    /**
    * Calculate the total size of all values in the hashCount map.
    *
    * @param hashCount The map containing the values.
    * @return The total size.
    */
    uint64_t calculateTotalSize(const robin_hood::unordered_map<std::string, uint64_t>& hashCount) {
    uint64_t totalSize = 0;
    for (const auto& kv : hashCount) {
    totalSize += kv.second;
    }
    return totalSize;
    }

    /**
     * Calculate the filter size based on the hash count and configuration.
     *
     * @param hashCount The map containing the hash count for each taxid.
     * @param icfConfig The configuration for the Interleaved Cuckoo Filter.
     * @param load_factor The desired load factor for the filter.
     * @param mode The mode of the calculation.
     */
	void calculateFilterSize(robin_hood::unordered_map<std::string, uint64_t>& hashCount, ICFConfig& icfConfig, double load_factor, std::string mode) {
		uint64_t maxValue = getMaxValue(hashCount);
		uint64_t totalSize = calculateTotalSize(hashCount);

		uint64_t minBinSize = 1;
		uint64_t maxBinSize = maxValue * 2; 
		uint64_t bestBinSize = maxBinSize;
		uint64_t bestBinNum = 0;
		double bestLoad = 0.0;

		while (minBinSize <= maxBinSize) {
			uint64_t binSize = (minBinSize + maxBinSize) / 2;
			uint64_t binNum = 0;

#pragma omp parallel for reduction(+:binNum)
			for (size_t idx = 0; idx < hashCount.size(); ++idx) {
				auto it = std::next(hashCount.begin(), idx);
				uint64_t count = it->second;
				binNum += (count + binSize - 1) / binSize;
			}

			double load = static_cast<double>(totalSize) / (binNum * binSize);

			if (load > load_factor) {
				minBinSize = binSize + 1;
			}
			else {
				bestBinSize = binSize;
				bestBinNum = binNum;
				bestLoad = load;
				if (load == load_factor) {
					break;
				}
				maxBinSize = binSize - 1;
			}
		}

		icfConfig.bins = bestBinNum;
		icfConfig.bin_size = bestBinSize;
	}
    /**
    * Calculate the bins mapped to the taxid.
    *
    * @param config The configuration for the Interleaved Cuckoo Filter.
	* @param hashCount The map containing the hash count for each taxid.
	* @return The map containing the bins mapped to the taxid.
	*/
	robin_hood::unordered_map<std::string, std::size_t> calculateTaxidMapBins(
		const ICFConfig& config,
		const robin_hood::unordered_map<std::string, uint64_t>& hashCount) {

		std::vector<std::pair<std::string, uint64_t>> taxid_count_vector;
		taxid_count_vector.reserve(hashCount.size());
		for (const auto& [taxid, count] : hashCount) {
			taxid_count_vector.emplace_back(taxid, count);
		}
		size_t num_taxids = taxid_count_vector.size();

		std::vector<std::size_t> binNums(num_taxids, 0);

#pragma omp parallel for schedule(static)
		for (size_t i = 0; i < num_taxids; ++i) {
			const auto& [taxid, count] = taxid_count_vector[i];
			binNums[i] = static_cast<std::size_t>(std::ceil(count / config.bin_size));
		}

		size_t num_threads = omp_get_max_threads();
		std::vector<std::size_t> prefixSum(num_taxids, 0);
		std::vector<std::size_t> threadSums(num_threads, 0);

#pragma omp parallel
		{
			int tid = omp_get_thread_num();
			size_t start = tid * num_taxids / num_threads;
			size_t end = (tid + 1) * num_taxids / num_threads;
			if (tid == num_threads - 1) {
				end = num_taxids;
			}

			size_t local_sum = 0;
			for (size_t i = start; i < end; ++i) {
				local_sum += binNums[i];
				prefixSum[i] = local_sum;
			}

			threadSums[tid] = local_sum;
		}

		std::vector<std::size_t> offsets(num_threads, 0);
		for (int i = 1; i < num_threads; ++i) {
			offsets[i] = offsets[i - 1] + threadSums[i - 1];
		}

#pragma omp parallel
		{
			int tid = omp_get_thread_num();
			size_t start = tid * num_taxids / num_threads;
			size_t end = (tid + 1) * num_taxids / num_threads;
			if (tid == num_threads - 1) {
				end = num_taxids;
			}

			size_t offset = offsets[tid];
			for (size_t i = start; i < end; ++i) {
				prefixSum[i] += offset;
			}
		}

		robin_hood::unordered_map<std::string, std::size_t> taxidBins;
		taxidBins.reserve(num_taxids);

		std::vector<std::pair<std::string, std::size_t>> temp_bins(num_taxids);

#pragma omp parallel for schedule(static)
		for (size_t i = 0; i < num_taxids; ++i) {
			temp_bins[i] = { taxid_count_vector[i].first, prefixSum[i] };
		}

		for (size_t i = 0; i < num_taxids; ++i) {
			taxidBins.emplace(temp_bins[i]);
		}

		return taxidBins;
	}

    /**
     * Process the taxid by inserting the hashes into the Interleaved Cuckoo Filter.
     *
     * @param taxid The taxid to process.
     * @param start The starting position in the filter.
     * @param end The ending position in the filter.
     * @param icf The Interleaved Cuckoo Filter to insert the hashes into.
     */
    void processTaxid(
        const std::string& taxid,
        size_t start,
        size_t end,
        chimera::InterleavedCuckooFilter& icf) {

        // Open the minimiser file for reading
        std::ifstream ifile("tmp/" + taxid + ".mini", std::ios::binary);
        if (ifile.fail()) {
            std::cerr << "Failed to open minimiser file: " << taxid << ".mini" << std::endl;
            return;
        }

        uint64_t hash;
        size_t currentPos = start;

        // Read the hashes from the file and insert them into the filter
        while (ifile.read(reinterpret_cast<char*>(&hash), sizeof(hash))) {
            // Insert the hash into the filter at the current position
            icf.insertTag(currentPos, hash);

            // Update the current position to the next position
            currentPos++;

            // If the current position reaches the end, reset it to the start
            if (currentPos == end) {
                currentPos = start;
            }
        }

        // Close the minimiser file
        ifile.close();

        // Delete the minimiser file
        std::filesystem::remove("tmp/" + taxid + ".mini");
    }

    /**
     * Build the Interleaved Cuckoo Filter.
     *
     * @param taxidBins The map containing the bins mapped to the taxid.
     * @param config The configuration for the Interleaved Cuckoo Filter.
     * @param icf The Interleaved Cuckoo Filter object to build.
     * @param hashCount The map containing the hash count for each taxid.
     * @param inputFiles The map containing the input files for each taxid.
     * @param numThreads The number of threads to use for building.
     */
    void build(
        const robin_hood::unordered_map<std::string, std::size_t>& taxidBins,
        ICFConfig config,
        chimera::InterleavedCuckooFilter& icf,
        const robin_hood::unordered_map<std::string, uint64_t>& hashCount,
        robin_hood::unordered_map<std::string, std::vector<std::string>> inputFiles,
        int numThreads) {

		std::vector<std::tuple<std::string, size_t, size_t>> taxid_info_pairs;
		taxid_info_pairs.reserve(hashCount.size());

		size_t previousEnd = 0;
		for (const auto& [taxid, count] : hashCount) {
			size_t currentEnd = taxidBins.at(taxid);
			taxid_info_pairs.emplace_back(taxid, previousEnd, currentEnd);
			previousEnd = currentEnd;
		}

#pragma omp parallel for schedule(dynamic) 
		for (size_t i = 0; i < taxid_info_pairs.size(); ++i) {
			const auto& [taxid, start, end] = taxid_info_pairs[i];

			processTaxid(taxid, start, end, icf);
		}
    }

    /**
     * Save the Interleaved Cuckoo Filter, ICFConfig, hashCount, and taxidBins to the output file.
     *
     * @param output_file The path to the output file.
     * @param icf The Interleaved Cuckoo Filter to be saved.
     * @param icfConfig The configuration for the Interleaved Cuckoo Filter.
     * @param hashCount The map containing the hash count for each taxid.
     * @param taxidBins The map containing the number of bins for each taxid.
     */
    void saveFilter(const std::string& output_file,
                    const chimera::InterleavedCuckooFilter& icf,
                    ICFConfig& icfConfig,
                    const robin_hood::unordered_map<std::string, uint64_t>& hashCount,
                    robin_hood::unordered_map<std::string, std::size_t> taxidBins) {

        // Open the output file
        std::ofstream os(output_file, std::ios::binary);

        // Check if the file is successfully opened
        if (!os.is_open()) {
            throw std::runtime_error("Failed to open file: " + output_file);
        }

        // Create a cereal binary archive
        cereal::BinaryOutputArchive archive(os);

        // Serialize the Interleaved Cuckoo Filter
        archive(icf);

        // Serialize the ICFConfig
        archive(icfConfig);

        // Manually convert and extract the robin_hood::unordered_map data to vector
        std::vector<std::pair<std::string, uint64_t>> hashCountData;
        for (const auto& kv : hashCount) {
            hashCountData.emplace_back(kv.first, kv.second);
        }

        std::vector<std::pair<std::string, std::size_t>> taxidBinsData;
        for (const auto& kv : taxidBins) {
            taxidBinsData.emplace_back(kv.first, kv.second);
        }

        // Serialize the vector
        archive(hashCountData);
        archive(taxidBinsData);

        // Close the file
        os.close();

        // Get the file size
        std::uintmax_t fileSize = std::filesystem::file_size(output_file);

        // Output the file size, formatted as KB, MB, or GB
        if (fileSize >= 1024 * 1024 * 1024) {
            std::cout << "Filter file size: " << std::fixed << std::setprecision(2)
                      << static_cast<double>(fileSize) / (1024 * 1024 * 1024) << " GB" << std::endl;
        } else if (fileSize >= 1024 * 1024) {
            std::cout << "Filter file size: " << std::fixed << std::setprecision(2)
                      << static_cast<double>(fileSize) / (1024 * 1024) << " MB" << std::endl;
        } else if (fileSize >= 1024) {
            std::cout << "Filter file size: " << std::fixed << std::setprecision(2)
                      << static_cast<double>(fileSize) / 1024 << " KB" << std::endl;
        } else {
            std::cout << "Filter file size: " << fileSize << " bytes" << std::endl;
        }
    }

	/**
	* Run the build process with the given build configuration.
	*
	* @param config The build configuration.
	*/
	void run(BuildConfig config) {
		if (config.verbose) {
			std::cout << config << std::endl;
		}
		omp_set_num_threads(config.threads);
		auto build_start = std::chrono::high_resolution_clock::now();

		auto read_start = std::chrono::high_resolution_clock::now();
		std::cout << "Reading input files..." << std::endl;
		ICFConfig icfConfig;
		FileInfo fileInfo;
		icfConfig.kmer_size = config.kmer_size;
		icfConfig.window_size = config.window_size;
		robin_hood::unordered_map<std::string, uint64_t> hashCount;
		robin_hood::unordered_map<std::string, std::vector<std::string>> inputFiles;
		parseInputFile(config.input_file, inputFiles, hashCount, fileInfo);
		auto read_end = std::chrono::high_resolution_clock::now();
		auto read_total_time = std::chrono::duration_cast<std::chrono::milliseconds>(read_end - read_start).count();
		if (config.verbose) {
			std::cout << "Read time: ";
			print_build_time(read_total_time);
			std::cout << std::endl;
		}


		auto calculate_start = std::chrono::high_resolution_clock::now();
		std::cout << "Calculating minimizers..." << std::endl;
		std::filesystem::path dir = "tmp";
		createOrResetDirectory(dir, config);
		minimiser_count(config, inputFiles, hashCount, fileInfo);
		auto calculate_end = std::chrono::high_resolution_clock::now();
		auto calculate_total_time = std::chrono::duration_cast<std::chrono::milliseconds>(calculate_end - calculate_start).count();
		if (config.verbose) {
			std::cout << "Calculate time: ";
			print_build_time(calculate_total_time);
			std::cout << "File information:" << std::endl;
			std::cout << "Number of files: " << fileInfo.fileNum << std::endl;
			std::cout << "Number of invalid files: " << fileInfo.invalidNum << std::endl;
			std::cout << "Number of sequences: " << fileInfo.sequenceNum << std::endl;
			std::cout << "Number of skipped sequences: " << fileInfo.skippedNum << std::endl;
			std::cout << "Total base pairs: " << fileInfo.bpLength << std::endl << std::endl;
		}


		auto calculate_filter_size_start = std::chrono::high_resolution_clock::now();
		std::cout << "Calculating filter size..." << std::endl;
		calculateFilterSize(hashCount, icfConfig, config.load_factor, config.mode);
		auto calculate_filter_size_end = std::chrono::high_resolution_clock::now();
		auto calculate_filter_size_total_time = std::chrono::duration_cast<std::chrono::milliseconds>(calculate_filter_size_end - calculate_filter_size_start).count();
		if (config.verbose) {
			std::cout << "Calculate filter size time: ";
			print_build_time(calculate_filter_size_total_time);
			std::cout << std::endl;
		}

		auto create_filter_start = std::chrono::high_resolution_clock::now();
		std::cout << "Creating filter..." << std::endl;
		chimera::InterleavedCuckooFilter icf(icfConfig.bins, icfConfig.bin_size);
		robin_hood::unordered_map<std::string, std::size_t> taxidBins = calculateTaxidMapBins(icfConfig, hashCount);
		build(taxidBins, icfConfig, icf, hashCount, inputFiles, config.threads);
		saveFilter(config.output_file, icf, icfConfig, hashCount, taxidBins);
		auto create_filter_end = std::chrono::high_resolution_clock::now();
		auto create_filter_total_time = std::chrono::duration_cast<std::chrono::milliseconds>(create_filter_end - create_filter_start).count();
		if (config.verbose) {
			std::cout << "Create filter time: ";
			print_build_time(create_filter_total_time);
			std::cout << std::endl;
		}

		auto build_end = std::chrono::high_resolution_clock::now();

		// Calculate the total build time in milliseconds
		auto build_total_time = std::chrono::duration_cast<std::chrono::milliseconds>(build_end - build_start).count();
		if (config.verbose) {
			std::cout << "Total build time: ";
			print_build_time(build_total_time);
			std::cout << icf << std::endl;
			
		}
	}
}