#define _CRT_SECURE_NO_WARNINGS  // Avoid issues with toml.h

#include "config.h"

#include <filesystem>

#include <toml/toml.h>

#include "log.h"
#include "../ASIO401Util/shell.h"

namespace asio401 {

	namespace {

		std::optional<toml::Value> LoadConfigToml() {
			const auto userDirectory = GetUserDirectory();
			if (!userDirectory.has_value()) {
				Log() << "Unable to determine user directory for configuration file";
				return toml::Table();
			}

			std::filesystem::path path(*userDirectory);
			path.append("ASIO401.toml");

			Log() << "Attempting to load configuration file: " << path;

			try {
				std::ifstream stream;
				stream.exceptions(stream.badbit | stream.failbit);
				try {
					stream.open(path);
				}
				catch (const std::exception& exception) {
					Log() << "Unable to open configuration file: " << exception.what();
					return toml::Table();
				}
				stream.exceptions(stream.badbit);

				const auto parseResult = toml::parse(stream);
				if (!parseResult.valid()) {
					Log() << "Unable to parse configuration file as TOML: " << parseResult.errorReason;
					return std::nullopt;
				}

				Log() << "Configuration file successfully parsed as valid TOML: " << parseResult.value;

				return parseResult.value;
			}
			catch (const std::exception& exception) {
				Log() << "Unable to read configuration file: " << exception.what();
				return std::nullopt;
			}
		}

		template <typename Functor> void ProcessOption(const toml::Table& table, const std::string& key, Functor functor) {
			const auto value = table.find(key);
			if (value == table.end()) return;
			try {
				return functor(value->second);
			}
			catch (const std::exception& exception) {
				throw std::runtime_error(std::string("in option '") + key + "': " + exception.what());
			}
		}

		template <typename T, typename Functor> void ProcessTypedOption(const toml::Table& table, const std::string& key, Functor functor) {
			return ProcessOption(table, key, [&](const toml::Value& value) { return functor(value.as<T>()); });
		}

		template <typename T> struct RemoveOptional { using Value = T; };
		template <typename T> struct RemoveOptional<std::optional<T>> { using Value = T; };

		template <typename T, typename Validator> void SetOption(const toml::Table& table, const std::string& key, T& option, Validator validator) {
			ProcessTypedOption<RemoveOptional<T>::Value>(table, key, [&](const RemoveOptional<T>::Value& value) {
				validator(value);
				option = value;
			});
		}
		template <typename T> void SetOption(const toml::Table& table, const std::string& key, T& option) {
			return SetOption(table, key, option, [](const T&) {});
		}

		void ValidateBufferSize(const int64_t& bufferSizeSamples) {
			if (bufferSizeSamples <= 0) throw std::runtime_error("buffer size must be strictly positive");
			if (bufferSizeSamples >= (std::numeric_limits<long>::max)()) throw std::runtime_error("buffer size is too large");
		}

		void SetConfig(const toml::Table& table, Config& config) {
			SetOption(table, "bufferSizeSamples", config.bufferSizeSamples, ValidateBufferSize);
		}

	}

	std::optional<Config> LoadConfig() {
		const auto tomlValue = LoadConfigToml();
		if (!tomlValue.has_value()) return std::nullopt;

		try {
			Config config;
			SetConfig(tomlValue->as<toml::Table>(), config);
			return config;
		}
		catch (const std::exception& exception) {
			Log() << "Invalid configuration: " << exception.what();
			return std::nullopt;
		}
	}

}