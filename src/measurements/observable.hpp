#pragma once

#include "timescale.hpp"
#include "../common.hpp"
#include "../matched_filter/ipp_matched_filter.hpp"
#include "../math/ipp_abs.hpp"
#include "../resample/resampler.hpp"
#include "../tracking/tracking_parameters.hpp"

#include <limits>
#include <optional>
#include <vector>

namespace ugsdr {
	class Observable final {
		Sv sv;
		std::vector<double> pseudorange;
		std::vector<double> pseudophase;
		std::vector<double> doppler;
		std::vector<double> snr;
		
		std::size_t preamble_position = std::numeric_limits<std::size_t>::max();

		template <typename T>
		Observable(const ugsdr::TrackingParameters<T>& tracking_result, TimeScale& time_scale) {}

		template <typename T>
		static auto GetPreambleGps(std::size_t size) {
			std::vector<std::complex<T>> preamble {
				1, -1, -1, -1, 1, -1, 1, 1
			};
			SequentialUpsampler::Transform(preamble, preamble.size() * 20);
			preamble.resize(size);
			return preamble;
		}

		template <typename T>
		static auto CheckParity(std::span<T> bits_src) {
			auto bits = std::vector<T>(bits_src.begin(), bits_src.end());
			if (bits[1] != 1) {
				for (std::size_t i = 2; i < 26; ++i)
					bits[i] *= -1;
			}
			std::array<T, 6> parity_values;
			// i'd love some fold expressions right here
			parity_values[1 - 1] = bits[1 - 1] * bits[3 - 1] * bits[4 - 1] * bits[5 - 1] * bits[7 - 1] *
				bits[8 - 1] * bits[12 - 1] * bits[13 - 1] * bits[14 - 1] * bits[15 - 1] *
				bits[16 - 1] * bits[19 - 1] * bits[20 - 1] * bits[22 - 1] * bits[25 - 1];

			parity_values[2 - 1] = bits[2 - 1] * bits[4 - 1] * bits[5 - 1] * bits[6 - 1] * bits[8 - 1] *
				bits[9 - 1] * bits[13 - 1] * bits[14 - 1] * bits[15 - 1] * bits[16 - 1] *
				bits[17 - 1] * bits[20 - 1] * bits[21 - 1] * bits[23 - 1] * bits[26 - 1];

			parity_values[3 - 1] = bits[1 - 1] * bits[3 - 1] * bits[5 - 1] * bits[6 - 1] * bits[7 - 1] *
				bits[9 - 1] * bits[10 - 1] * bits[14 - 1] * bits[15 - 1] * bits[16 - 1] *
				bits[17 - 1] * bits[18 - 1] * bits[21 - 1] * bits[22 - 1] * bits[24 - 1];

			parity_values[4 - 1] = bits[2 - 1] * bits[4 - 1] * bits[6 - 1] * bits[7 - 1] * bits[8 - 1] *
				bits[10 - 1] * bits[11 - 1] * bits[15 - 1] * bits[16 - 1] * bits[17 - 1] *
				bits[18 - 1] * bits[19 - 1] * bits[22 - 1] * bits[23 - 1] * bits[25 - 1];

			parity_values[5 - 1] = bits[2 - 1] * bits[3 - 1] * bits[5 - 1] * bits[7 - 1] * bits[8 - 1] *
				bits[9 - 1] * bits[11 - 1] * bits[12 - 1] * bits[16 - 1] * bits[17 - 1] *
				bits[18 - 1] * bits[19 - 1] * bits[20 - 1] * bits[23 - 1] * bits[24 - 1] *
				bits[26 - 1];

			parity_values[6 - 1] = bits[1 - 1] * bits[5 - 1] * bits[7 - 1] * bits[8 - 1] * bits[10 - 1] *
				bits[11 - 1] * bits[12 - 1] * bits[13 - 1] * bits[15 - 1] * bits[17 - 1] *
				bits[21 - 1] * bits[24 - 1] * bits[25 - 1] * bits[26 - 1];

			auto parity_check = std::span(bits.begin() + 26, 6);
			if (std::equal(parity_values.begin(), parity_values.end(), parity_check.begin(), parity_check.end()))
				return true;

			return false;
		}

		template <typename T>
		static auto GetAccumulatedBits(std::span<const T> arr) {
			auto accumulated_bits = IppAccumulator::Transform(arr, 20);
			for (auto& el : accumulated_bits)
				el = el > 0 ? 1 : -1;
			return accumulated_bits;
		}

		template <typename T>
		static std::optional<std::size_t> FindPreamblePosition(const std::vector<std::size_t>& indexes, std::span<const T> bits) {
			auto preamble_position = std::numeric_limits<std::size_t>::max();
			for (auto& el : indexes) {
				auto it = std::find_if(indexes.begin(), indexes.end(), [el](auto& ind) { return el - ind == 6000; });
				if (it == indexes.end())
					continue;

				auto accumulated_bits = GetAccumulatedBits(std::span(bits.begin() + *it - 40, 20 * 62));
				if (CheckParity(std::span(accumulated_bits.begin(), 32)) && CheckParity(std::span(accumulated_bits.begin() + 30, 32))) {
					preamble_position = *it;
					// if (pseudorange[0] < 0.5) ++preamble_position;
					return preamble_position;
				}
			}

			return std::nullopt;
		}

		template <typename T>
		static auto FindPreambleGps(const ugsdr::TrackingParameters<T>& tracking_result) {
			std::vector<T> navigation_bits;
			const auto& prompt = tracking_result.prompt;
			navigation_bits.reserve(prompt.size());
			std::transform(prompt.begin(), prompt.end(), std::back_inserter(navigation_bits), [](auto& prompt_value) {
				auto value = prompt_value.real() > 0 ? 1 : -1;
				return static_cast<T>(value);
			});

			const auto preamble = GetPreambleGps<T>(prompt.size());
			const auto bits = std::vector<std::complex<T>>(navigation_bits.begin(), navigation_bits.end());
			auto abs_corr = IppAbs::Transform(IppMatchedFilter::Filter(bits, preamble));
			std::vector<std::size_t> indexes;
			for (std::size_t i = 0; i < abs_corr.size(); ++i)
				if (abs_corr[i] > 153)
					indexes.push_back(i);

			std::vector<T> vals;
			for (auto& el : prompt)
				vals.push_back(el.real());
			auto preamble_position = FindPreamblePosition(indexes, std::span<const T>(vals));
			if (!preamble_position)
				return 0;

			auto nav_bits = GetAccumulatedBits(std::span<const T>(navigation_bits.begin() + preamble_position.value() - 20, 1501 * 20));

			return 0;
		}
		template <typename T>
		static auto FindPreamble(const ugsdr::TrackingParameters<T>& tracking_result) {
			switch (tracking_result.sv.system) {
			case (System::Gps):
				return FindPreambleGps(tracking_result);
				break;
			case (System::Glonass):
				//break;
			default:
				throw std::runtime_error("Unsupported system");
			}
		}

	public:
		template <typename T>
		static auto MakeObservable(const ugsdr::TrackingParameters<T>& tracking_result, TimeScale& time_scale) -> std::optional<Observable> {
			auto placeholder = FindPreamble(tracking_result);

			return std::nullopt;
		}
	};
}