#pragma once

#pragma pack(push)

#include "is_complex.hpp"

#include <arrayfire.h>
#include <optional>
#include <variant>

namespace ugsdr {
	struct ArrayProxy {
	private:
		af::array array;

		auto GetVariant() const {
			std::variant<
				std::vector<float>,
				std::vector<std::complex<float>>,
				std::vector<double>,
				std::vector<std::complex<double>>,
				std::vector<std::int8_t>,
				std::vector<std::int32_t>,
				std::vector<std::uint32_t>,
				std::vector<std::uint8_t>,
				std::vector<std::int64_t>,
				std::vector<std::uint64_t>,
				std::vector<std::int16_t>,
				std::vector<std::uint16_t>
			> dst;

			switch (array.type()) {
			case f32:
				dst = std::vector<float>(array.elements());
				break;
			case c32:
				dst = std::vector<std::complex<float>>(array.elements());
				break;
			case f64:
				dst = std::vector<double>(array.elements());
				break;
			case c64:
				dst = std::vector<std::complex<double>>(array.elements());
				break;
			case b8:
				dst = std::vector<std::int8_t>(array.elements());
				break;
			case s32:
				dst = std::vector<std::int32_t>(array.elements());
				break;
			case u32:
				dst = std::vector<std::uint32_t>(array.elements());
				break;
			case u8:
				dst = std::vector<std::uint8_t>(array.elements());
				break;
			case s64:
				dst = std::vector<std::int64_t>(array.elements());
				break;
			case u64:
				dst = std::vector<std::uint64_t>(array.elements());
				break;
			case s16:
				dst = std::vector<std::int16_t>(array.elements());
				break;
			case u16:
				dst = std::vector<std::uint16_t>(array.elements());
				break;
			case f16:
			default:
				throw std::runtime_error("Unexpected type");
			}
			std::visit([this](auto& vec) {
				array.host(vec.data());
				}, dst);

			return dst;
		}
	public:
		template <typename ...Args>
		ArrayProxy(Args&& ... args) : array(args...) {}

		template <typename T>
		ArrayProxy(const std::vector<T>& vec) : array(vec.size(), vec.data()) {}

		template <typename T>
		ArrayProxy(const std::vector<std::complex<T>>& vec) {
			if constexpr (std::is_same_v<T, double>)
				array = af::array(vec.size(), reinterpret_cast<const af::cdouble*>(vec.data()));
			else if constexpr (std::is_same_v<T, float>)
				array = af::array(vec.size(), reinterpret_cast<const af::cfloat*>(vec.data()));
			else {
				auto converted_vector = std::vector<std::complex<float>>(vec.begin(), vec.end());
				array = af::array(converted_vector.size(), reinterpret_cast<const af::cfloat*>(converted_vector.data()));
			}
		}

		operator af::array& () {
			return array;
		}

		operator const af::array& () const {
			return array;
		}
				
		template <typename T = std::complex<double>>
		operator std::vector<T>() const {
			std::vector<T> dst;

			auto type_erased_vector = GetVariant();

			std::visit([&dst](auto&& vec) {
				if constexpr (std::is_same_v<std::remove_reference_t<decltype(vec[0])>, T>) {
					dst = std::move(vec);
					return;
				}

				constexpr bool is_src_complex = is_complex_v<std::remove_reference_t<decltype(vec[0])>>;
				constexpr bool is_dst_complex = is_complex_v<T>;

				if constexpr (is_src_complex && !is_dst_complex) {
					for (auto&& el : vec)
						dst.emplace_back(static_cast<underlying_t<T>>(el.real()));
				}
				else
					for (auto&& el : vec)
						dst.emplace_back(static_cast<std::conditional_t<is_src_complex&& is_dst_complex, T, underlying_t<T>>>(el));
				}, type_erased_vector);

			return dst;
		}


		template <typename T>
		std::optional<std::vector<T>> CopyFromGpu(std::vector<T>& optional_dst) const {
			// type-based map would work nicely here
			switch (array.type()) {
			case c32:
				if (std::is_same_v<T, std::complex<float>>) {
					optional_dst.resize(array.elements());
					array.host(optional_dst.data());
					return std::nullopt;
				}
				break;
			case c64:
				if (std::is_same_v<T, std::complex<double>>) {
					optional_dst.resize(array.elements());
					array.host(optional_dst.data());
					return std::nullopt;
				}
				break;
			case f64:
			case b8:
			case s32:
			case u32:
			case u8:
			case s64:
			case u64:
			case s16:
			case u16:
			case f16:
			case f32:
			default:
				break;
			}
			return static_cast<std::vector<T>>(*this);
		}

		[[nodiscard]]
		std::size_t size() const {
			return array.elements();
		}
	};

}

#pragma pack(pop)