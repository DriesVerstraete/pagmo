/*****************************************************************************
 *   Copyright (C) 2008, 2009 Advanced Concepts Team (European Space Agency) *
 *   act@esa.int                                                             *
 *                                                                           *
 *   This program is free software; you can redistribute it and/or modify    *
 *   it under the terms of the GNU General Public License as published by    *
 *   the Free Software Foundation; either version 2 of the License, or       *
 *   (at your option) any later version.                                     *
 *                                                                           *
 *   This program is distributed in the hope that it will be useful,         *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of          *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the           *
 *   GNU General Public License for more details.                            *
 *                                                                           *
 *   You should have received a copy of the GNU General Public License       *
 *   along with this program; if not, write to the                           *
 *   Free Software Foundation, Inc.,                                         *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.               *
 *****************************************************************************/

// 04/01/2009: Initial version by Francesco Biscani.

#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/thread/thread.hpp>
#include <iostream>
#include <stdexcept>

#include "../../atomic_counters/atomic_counters.h"
#include "../../exceptions.h"
#include "../algorithms/go_algorithm.h"
#include "../problems/GOproblem.h"
#include "archipelago.h"
#include "individual.h"
#include "island.h"
#include "population.h"

PaGMO::atomic_counter_size_t island::id_counter(1);

size_t island::get_new_id()
{
	return (size_t)(id_counter++);
}

island::island(const GOProblem &p, const go_algorithm &al):m_id(get_new_id()),m_pop(p),m_goa(al.clone()),m_a(0),m_evo_time(0) {}

island::island(const GOProblem &p, const go_algorithm &al, int n):m_id(get_new_id()),m_pop(p,n),m_goa(al.clone()),m_a(0),m_evo_time(0) {}

island::island(const island &i):m_id(get_new_id()),m_pop(i.population()),m_goa(i.m_goa->clone()),m_a(0),m_evo_time(i.m_evo_time) {}

island &island::operator=(const island &i)
{
	if (this != &i) {
		m_pop = i.population();
		m_id = get_new_id();
		m_goa.reset(i.m_goa->clone());
		m_evo_time = i.m_evo_time;
	}
	return *this;
}

island::~island()
{
	join();
}

const GOProblem &island::problem() const
{
	// We must lock here because problem is embedded into population: at the end of an evolution
	// we overwrite the population, so we may have simultaneous read-write on the problem.
	join();
	return m_pop.problem();
}

const go_algorithm &island::algorithm() const
{
	return *m_goa;
}

void island::set_algorithm(const go_algorithm &a)
{
	join();
	m_goa.reset(a.clone());
}

void island::evolve(int N)
{
	if (m_mutex.try_lock()) {
		boost::thread(int_evolver(this,N));
	} else {
// WORKAROUND: apparently there are some issues here with  exception throwing
// under MinGW when evolution is running in another thread. This needs to be investigated.
#ifdef PAGMO_WIN32
		std::cout << "Cannot evolve while still evolving!\n";
#else
		pagmo_throw(std::runtime_error,"cannot evolve while still evolving");
#endif
	}
}

void island::evolve_t(const size_t &t)
{
	if (m_mutex.try_lock()) {
		boost::thread(t_evolver(this,t));
	} else {
#ifdef PAGMO_WIN32
		std::cout << "Cannot evolve while still evolving!\n";
#else
		pagmo_throw(std::runtime_error,"cannot evolve while still evolving");
#endif
	}
}

void island::join() const {
	lock_type lock(m_mutex);
}

bool island::busy() const {
	if (!m_mutex.try_lock()) {
		return true;
	}
	m_mutex.unlock();
	return false;
}

size_t island::id() const
{
	return m_id;
}

size_t island::size() const
{
	join();
	return m_pop.size();
}

double island::mean() const
{
	join();
	return m_pop.evaluateMean();
}

double island::std() const
{
	join();
	return m_pop.evaluateStd();
}

Individual island::best() const
{
	join();
	return m_pop.extractBestIndividual();
}

Individual island::worst() const
{
	join();
	return m_pop.extractBestIndividual();
}

Population island::population() const
{
	join();
	return m_pop;
}

Individual island::operator[](int n) const
{
	join();
	return m_pop[n];
}

void island::set(int n, const Individual &i)
{
	join();
	m_pop[n] = i;
}

void island::push_back(const Individual &i)
{
	join();
	m_pop.push_back(i);
}

void island::insert(int n, const Individual &i)
{
	join();
	m_pop.insert(n,i);
}

void island::erase(int n)
{
	join();
	m_pop.erase(n);
}

size_t island::evo_time() const
{
	join();
	return m_evo_time;
}

// NOTE: no lock needed here because this is intended to be called only by archipelago when
// adding an island.
void island::set_archipelago(archipelago *a)
{
	m_a = a;
}

void island::int_evolver::operator()()
{
	const boost::posix_time::ptime start = boost::posix_time::microsec_clock::local_time();
	try {
		if (m_i->m_a) {
			m_i->m_a->m_top->pre_evolution(*m_i);
		}
		for (int i = 0; i < m_n; ++i) {
			m_i->m_pop = m_i->m_goa->evolve(m_i->m_pop);
			//std::cout << "Evolution finished, best fitness is: " << m_i->m_pop.extractBestIndividual().getFitness() << '\n';
			if (m_i->m_a) {
				m_i->m_a->m_top->post_evolution(*m_i);
			}
		}
	} catch (const std::exception &e) {
		std::cout << "Error during evolution: " << e.what() << '\n';
	} catch (...) {
		std::cout << "Unknown exception caught. :(\n";
	}
	// We must take care of potentially low-accuracy clocks, where the time difference could be negative for
	// _really_ short evolution times. In that case do not add anything to the total evolution time.
	const boost::posix_time::time_duration diff = boost::posix_time::microsec_clock::local_time() - start;
	if (diff.total_milliseconds() >= 0) {
		m_i->m_evo_time += diff.total_milliseconds();
	}
	m_i->m_mutex.unlock();
}

// Perform at least one evolution, and continue evolving until at least a certain amount of time has passed.
void island::t_evolver::operator()()
{
	const boost::posix_time::ptime start = boost::posix_time::microsec_clock::local_time();
	boost::posix_time::time_duration diff;
	try {
		if (m_i->m_a) {
			m_i->m_a->m_top->pre_evolution(*m_i);
		}
		do {
			m_i->m_pop = m_i->m_goa->evolve(m_i->m_pop);
			diff = boost::posix_time::microsec_clock::local_time() - start;
			//std::cout << "Evolution finished, best fitness is: " << m_i->m_pop.extractBestIndividual().getFitness() << '\n';
			// Take care of negative timings.
			if (m_i->m_a) {
				m_i->m_a->m_top->post_evolution(*m_i);
			}
		} while (diff.total_milliseconds() < 0 || (size_t)diff.total_milliseconds() < m_t);
	} catch (const std::exception &e) {
		std::cout << "Error during evolution: " << e.what() << '\n';
	} catch (...) {
		std::cout << "Unknown exception caught. :(\n";
	}
	m_i->m_evo_time += diff.total_milliseconds();
	m_i->m_mutex.unlock();
}

std::ostream &operator<<(std::ostream &s, const island &isl)
{
	s << "ID:              " << isl.id() << '\n';
	s << "Population size: " << isl.size() << '\n';
	s << "Evolution time:  " << isl.evo_time() << '\n';
	s << "Algorithm type:  " << isl.algorithm().id_name() << '\n';
	boost::lock_guard<boost::mutex> lock(isl.m_mutex);
	s << isl.m_pop;
	return s;
}
