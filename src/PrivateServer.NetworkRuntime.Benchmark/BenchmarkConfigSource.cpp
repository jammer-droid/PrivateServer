#include "BenchmarkConfigSource.h"

#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace psnr::benchmark
{
    namespace
    {
        constexpr std::uintmax_t MaximumConfigBytes = 1024 * 1024;

        [[nodiscard]] BenchmarkConfigResolveResult Failure(std::string error)
        {
            BenchmarkConfigResolveResult result;
            result.error = std::move(error);
            return result;
        }

        [[nodiscard]] BenchmarkConfigResolveResult ResolveCanonical()
        {
            BenchmarkResolvedConfig resolved;
            resolved.config = BenchmarkConfigV1Codec::Canonical();

            const std::string validationError = BenchmarkConfigV1Codec::Validate(resolved.config);
            if (!validationError.empty())
            {
                return Failure("canonical config is invalid: " + validationError);
            }

            resolved.normalizedJson = BenchmarkConfigV1Codec::SerializeNormalized(resolved.config);

            BenchmarkConfigResolveResult result;
            result.resolved = std::move(resolved);
            return result;
        }

        [[nodiscard]] BenchmarkConfigResolveResult ResolveFile(const std::string_view configPath)
        {
            const std::u8string encodedPath(configPath.cbegin(), configPath.cend());
            const std::filesystem::path path(encodedPath);

            std::error_code fileSizeError;
            const std::uintmax_t fileSize = std::filesystem::file_size(path, fileSizeError);
            if (fileSizeError)
            {
                return Failure("config file size is unavailable: " + std::string(configPath));
            }
            if (fileSize > MaximumConfigBytes || fileSize > std::numeric_limits<std::size_t>::max())
            {
                return Failure("config file exceeds the 1 MiB limit: " + std::string(configPath));
            }

            std::ifstream input(path, std::ios::binary);
            if (!input.is_open())
            {
                return Failure("config file open failed: " + std::string(configPath));
            }

            std::string jsonText(static_cast<std::size_t>(fileSize), '\0');
            if (!jsonText.empty())
            {
                input.read(jsonText.data(), static_cast<std::streamsize>(jsonText.size()));
                if (input.gcount() != static_cast<std::streamsize>(jsonText.size()) || input.bad())
                {
                    return Failure("config file read failed: " + std::string(configPath));
                }
            }

            BenchmarkConfigParseResult parseResult = BenchmarkConfigV1Codec::Parse(jsonText);
            if (!parseResult.Succeeded())
            {
                return Failure("config validation failed: " + parseResult.error);
            }

            BenchmarkResolvedConfig resolved;
            resolved.config = std::move(parseResult.config);
            resolved.normalizedJson = BenchmarkConfigV1Codec::SerializeNormalized(resolved.config);
            resolved.loadedFromFile = true;

            BenchmarkConfigResolveResult result;
            result.resolved = std::move(resolved);
            return result;
        }
    } // namespace

    BenchmarkConfigResolveResult BenchmarkConfigSource::Resolve(const std::string_view configPath)
    {
        try
        {
            return configPath.empty() ? ResolveCanonical() : ResolveFile(configPath);
        }
        catch (const std::exception& exception)
        {
            return Failure("config resolution failed: " + std::string(exception.what()));
        }
        catch (...)
        {
            return Failure("config resolution failed with an unknown error");
        }
    }
} // namespace psnr::benchmark
