#ifndef KEPLERIAN_TOOLBOX_STATE_H
#define KEPLERIAN_TOOLBOX_STATE_H

#include <boost/array.hpp>
#include <boost/numeric/conversion/cast.hpp>
#include <boost/static_assert.hpp>
#include <boost/scoped_ptr.hpp>
#include <boost/shared_ptr.hpp>
#include <cmath>
#include <iostream>
#include <vector>

#include <p_exceptions.h>
#include "types.h"

namespace keplerian_toolbox
{
	template <class T, int Size>
	class state {
			BOOST_STATIC_ASSERT(Size > 0);
		public:
			typedef T value_type;
			typedef size_t size_type;
			state()
			{
				for (size_type i = 0; i < Size; ++i) {
					m_array[i] = value_type(0);
				}
			}
			template <class Vector>
			state(const Vector &v)
			{
				if (v.size() != Size) {
					P_EX_THROW(value_error,"invalid vector size while constructing state");
				}
				for (size_type i = 0; i < Size; ++i) {
					m_array[i] = value_type(v[i]);
				}
			}
			size_type size() const
			{
				return Size;
			}
			const value_type &operator[](const size_type &n) const
			{
				return m_array[n];
			}
		protected:
			boost::array<value_type,Size> m_array;
	};

	template <class T, int Size>
	inline std::ostream &operator<<(std::ostream &o, const state<T,Size> &s)
	{
		typedef typename state<T,Size>::size_type size_type;
		o << std::scientific;
		o.precision(15);
		o << "State vector: [";
		for (size_type i = 0; i < Size; ++i)
		{
			o << s[i];
			if (i != Size - 1) {
				o << ' ';
			}
		}
		o << "]\n";
		return o;
	}

	template <class T>
	struct base_coordinate_system {
		virtual void to_cartesian(boost::array<T,6> &) const {}
		virtual void from_cartesian(boost::array<T,6> &) const {}
		virtual boost::shared_ptr<base_coordinate_system> clone() const = 0;
		virtual ~base_coordinate_system() {}
	};

	template <class T>
	struct cartesian_coordinate_system: public base_coordinate_system<T> {
		boost::shared_ptr<base_coordinate_system<T> > clone() const
		{
			return boost::shared_ptr<base_coordinate_system<T> >(new cartesian_coordinate_system());
		}
	};

	template <class T>
	struct spherical_coordinate_system: public base_coordinate_system<T> {
		void to_cartesian(boost::array<T,6> &s) const
		{
			// Position.
			const T &r = s[0], &phi = s[1], &theta = s[2], sin_theta = std::sin(theta),
				cos_theta = std::cos(theta), sin_phi = std::sin(phi), cos_phi = std::cos(phi);
			const T x = r * sin_theta * cos_phi, y = r * sin_theta * sin_phi, z = r * cos_theta;
			// Velocity.
			const T &v = s[0], &vphi = s[1], &vtheta = s[2], sin_vtheta = std::sin(vtheta),
				cos_vtheta = std::cos(vtheta), sin_vphi = std::sin(vphi), cos_vphi = std::cos(vphi);
			const T vx = v * sin_vtheta * cos_vphi, vy = v * sin_vtheta * sin_vphi, vz = v * cos_vtheta;
			s[0] = x;
			s[1] = y;
			s[2] = z;
			s[3] = vx;
			s[4] = vy;
			s[5] = vz;
		}
		void from_cartesian(boost::array<T,6> &s) const
		{
			// Position.
			const T &x = s[0], &y = s[1], &z = s[2];
			const T r = std::sqrt(x * x + y * y + z * z);
			T phi, theta;
			// If r is zero, assign zero to the other guys by convention.
			if (r == 0) {
				phi = theta = T(0);
			} else {
				phi = std::atan2(y,x);
				theta = std::acos(z / r);
			}
			// Velocity.
			const T &vx = s[3], &vy = s[4], &vz = s[5];
			const T v = std::sqrt(vx * vx + vy * vy + vz * vz);
			T vphi, vtheta;
			if (v == 0) {
				vphi = vtheta = T(0);
			} else {
				vphi = std::atan2(vy,vx);
				vtheta = std::acos(vz / v);
			}
			s[0] = r;
			s[1] = phi;
			s[2] = theta;
			s[3] = v;
			s[4] = vphi;
			s[5] = vtheta;
		}
		boost::shared_ptr<base_coordinate_system<T> > clone() const
		{
			return boost::shared_ptr<base_coordinate_system<T> >(new spherical_coordinate_system());
		}
	};

	template <class T>
	class pv_state: public state<T,6> {
			typedef state<T,6> ancestor;
		public:
			pv_state():ancestor(),m_cs(new cartesian_coordinate_system<T>()) {}
			template <class Vector>
			pv_state(const Vector &v):ancestor(v),m_cs(new cartesian_coordinate_system<T>()) {}
			template <class Vector>
			pv_state(const Vector &pos, const Vector &vel):ancestor(),m_cs(new cartesian_coordinate_system<T>())
			{
				pv_size_check(pos);
				pv_size_check(vel);
				typedef typename ancestor::size_type size_type;
				for (size_type i = 0; i < 3; ++i) {
					this->m_array[i] = pos[i];
					this->m_array[i + 3] = vel[i];
				}
			}
			pv_state(const pv_state &pvs):ancestor(pvs),m_cs(pvs.m_cs->clone()) {}
			pv_state &operator=(const pv_state &pvs)
			{
				if (this != &pvs) {
					ancestor::operator=(pvs);
					m_cs = pvs.m_cs->clone();
				}
				return *this;
			}
			boost::array<T,3> get_position() const
			{
				boost::array<T,3> retval = {{(*this)[0], (*this)[1], (*this)[2]}};
				return retval;
			}
			boost::array<T,3> get_velocity() const
			{
				boost::array<T,3> retval = {{(*this)[3], (*this)[4], (*this)[5]}};
				return retval;
			}
			boost::shared_ptr<base_coordinate_system<T> > get_coordinate_system() const
			{
				return m_cs->clone();
			}
			pv_state &set_coordinate_system(const base_coordinate_system<T> &cs)
			{
				m_cs->to_cartesian(this->m_array);
				m_cs = cs.clone();
				m_cs->from_cartesian(this->m_array);
				return *this;
			}
			template <class Vector>
			void set_position(const Vector &p)
			{
				pv_size_check(p);
				this->m_array[0] = p[0];
				this->m_array[1] = p[1];
				this->m_array[2] = p[2];
			}
			template <class Vector>
			void set_velocity(const Vector &v)
			{
				pv_size_check(v);
				this->m_array[3] = v[0];
				this->m_array[4] = v[1];
				this->m_array[5] = v[2];
			}
		private:
			template <class Vector>
			static void pv_size_check(const Vector &v)
			{
				if (v.size() != 3) {
					P_EX_THROW(value_error,"invalid size for position/velocity vector");
				}
			}
			boost::shared_ptr<base_coordinate_system<T> > m_cs;
	};

	template <class T>
	struct base_orbit_propagator {
		virtual void propagate(std::vector<pv_state<T> > &) const {}
		virtual bool verify(const std::vector<pv_state<T> > &) const
		{
			return true;
		}
		virtual boost::shared_ptr<base_orbit_propagator> clone() const = 0;
		virtual ~base_orbit_propagator() {}
	};

	template <class T>
	struct null_orbit_propagator: public base_orbit_propagator<T> {
		boost::shared_ptr<base_orbit_propagator<T> > clone() const
		{
			return boost::shared_ptr<base_orbit_propagator<T> >(new null_orbit_propagator());
		}
	};

	template <class T>
	class dynamical_system {
		public:
			dynamical_system():m_op(new null_orbit_propagator<T>()),m_states(),m_time(0)
			{
				if (!m_op->verify(m_states)) {
					
				}
			}
			dynamical_system(const dynamical_system &b):m_op(b.m_op->clone()),m_states(b.m_states),m_time(b.m_time) {}
			dynamical_system &operator=(const dynamical_system &b)
			{
				if (this != &b) {
					m_op = b.m_op->clone();
					m_states = b.m_states;
					m_time = b.m_time;
				}
				return *this;
			}
			size_t size() const
			{
				return m_states.size();
			}
		private:
			boost::shared_ptr<base_orbit_propagator<T> >	m_op;
			std::vector<pv_state<T>	>			m_states;
			double						m_time;
	};
}

#endif
