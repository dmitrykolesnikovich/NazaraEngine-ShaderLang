// Copyright (C) 2022 Jérôme "Lynix" Leclercq (lynix680@gmail.com)
// This file is part of the "Nazara Shading Language" project
// For conditions of distribution and use, see copyright notice in Config.hpp

#pragma once

#ifndef NZSL_VECTOR_HPP
#define NZSL_VECTOR_HPP

#include <NZSL/Config.hpp>
#include <Nazara/Utils/Algorithm.hpp>
#include <array>

namespace nzsl
{
	template<typename T, std::size_t N>
	struct Vector
	{
		static constexpr std::size_t Dimensions = N;
		using Base = T;

		Vector() = default;
		explicit Vector(T x);
		explicit Vector(T x, T y);
		explicit Vector(T x, T y, T z);
		explicit Vector(T x, T y, T z, T w);

		T& x();
		T x() const;
		
		T& y();
		T y() const;

		T& z();
		T z() const;

		T& w();
		T w() const;

		T& operator[](std::size_t i);
		T operator[](std::size_t i) const;

		Vector operator-() const;

		Vector operator*(T rhs) const;
		Vector operator/(T rhs) const;

		Vector operator+(const Vector& vec) const;
		Vector operator-(const Vector& vec) const;
		Vector operator*(const Vector& vec) const;
		Vector operator/(const Vector& vec) const;

		bool operator==(const Vector& vec) const;
		bool operator!=(const Vector& vec) const;

		std::array<T, N> values;
	};

	template<typename T, std::size_t N>
	Vector<T, N> operator*(T lhs, const Vector<T, N>& rhs);

	template<typename T, std::size_t N>
	Vector<T, N> operator/(T lhs, const Vector<T, N>& rhs);

	template<typename T> using Vector2 = Vector<T, 2>;
	using Vector2d   = Vector<double, 2>;
	using Vector2f   = Vector<float, 2>;
	using Vector2i32 = Vector<std::int32_t, 2>;
	using Vector2i64 = Vector<std::int64_t, 2>;
	using Vector2u32 = Vector<std::uint32_t, 2>;
	using Vector2u64 = Vector<std::uint64_t, 2>;

	template<typename T> using Vector3 = Vector<T, 3>;
	using Vector3d = Vector<double, 3>;
	using Vector3f   = Vector<float, 3>;
	using Vector3i32 = Vector<std::int32_t, 3>;
	using Vector3i64 = Vector<std::int64_t, 3>;
	using Vector3u32 = Vector<std::uint32_t, 3>;
	using Vector3u64 = Vector<std::uint64_t, 3>;

	template<typename T> using Vector4 = Vector<T, 4>;
	using Vector4d = Vector<double, 4>;
	using Vector4f   = Vector<float, 4>;
	using Vector4i32 = Vector<std::int32_t, 4>;
	using Vector4i64 = Vector<std::int64_t, 4>;
	using Vector4u32 = Vector<std::uint32_t, 4>;
	using Vector4u64 = Vector<std::uint64_t, 4>;
}

#include <NZSL/Vector.inl>

#endif // NZSL_VECTOR_HPP