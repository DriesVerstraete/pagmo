/*****************************************************************************
 *   Copyright (C) 2004-2009 The PaGMO development team,                     *
 *   Advanced Concepts Team (ACT), European Space Agency (ESA)               *
 *   http://apps.sourceforge.net/mediawiki/pagmo                             *
 *   http://apps.sourceforge.net/mediawiki/pagmo/index.php?title=Developers  *
 *   http://apps.sourceforge.net/mediawiki/pagmo/index.php?title=Credits     *
 *   act@esa.int                                                             *
 *                                                                           *
 *   This program is free software; you can redistribute it and/or modify    *
 *   it under the terms of the GNU General Public License as published by    *
 *   the Free Software Foundation; either version 3 of the License, or       *
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

#include <climits>
#include <iostream>
#include <fstream>
#include <vector>
#include <list>

#include "src/algorithms.h"
#include "src/archipelago.h"
#include "src/island.h"
#include "src/problems.h"
#include "src/topologies.h"
#include "src/archipelago.h"
#include "src/problem/base.h"
#include "src/keplerian_toolbox/keplerian_toolbox.h"

#include <boost/algorithm/string.hpp>
#include <boost/lexical_cast.hpp>


using namespace pagmo;

static const int mpcorb_format[8][2] =
{
	{92,11},	// a (AU)
	{70,9},		// e
	{59,9},		// i (deg)
	{48,9},		// Omega (deg)
	{37,9},		// omega (deg)
	{26,9},		// M (deg)
	{20,5},		// Epoch (shitty format)
	{166,28}	// Asteroid readable name

};

// Convert mpcorb packed dates conventio into number.
// TODO: check locale ASCII.
static inline int packed_date2number(char c)
{
	return static_cast<int>(c) - (boost::algorithm::is_alpha()(c) ? 55 : 48);
}

int main()
{
	int n_multistart = 1;
	ofstream myfile;
	myfile.open ("out.pagmo");

	pagmo::algorithm::sa_corana algo1(10000,1,0.01);
	pagmo::algorithm::de algo2(500,0.8,0.8,3);
	pagmo::algorithm::nlopt_sbplx algo3(500,1e-4);

	std::ifstream mpcorbfile("MPCORB.DAT");
	boost::array<double,6> elem;
	std::string tmp;
	std::string line;
	do {
		std::getline(mpcorbfile,line);
	} while (!boost::algorithm::find_first(line,"-----------------"));

	while (!mpcorbfile.eof()) {
		std::getline(mpcorbfile,line);
		for (int i = 0; i < 6; ++i) {
			tmp.clear();
			tmp.append(&line[mpcorb_format[i][0]],mpcorb_format[i][1]);
			boost::algorithm::trim(tmp);
			elem[i] = boost::lexical_cast<double>(tmp);
		}

		if (!(elem[0] < 2.3 && elem[1] < 0.3 && elem[2] < 15)) {
			continue;
		}

		// Converting orbital elements to the dictatorial PaGMO units.
		elem[0] *= ASTRO_AU;
		for (int i = 2; i < 6; ++i) {
			elem[i] *= ASTRO_DEG2RAD;
		}


		// Deal with shitty epoch format.
		tmp.clear();
		tmp.append(&line[mpcorb_format[6][0]],mpcorb_format[6][1]);
		boost::algorithm::trim(tmp);
		boost::gregorian::greg_year anno = packed_date2number(tmp[0]) * 100 + boost::lexical_cast<int>(std::string(&tmp[1],&tmp[3]));
		boost::gregorian::greg_month mese = packed_date2number(tmp[3]);
		boost::gregorian::greg_day giorno = packed_date2number(tmp[4]);

		// Record asteroid name.
		tmp.clear();
		tmp.append(&line[mpcorb_format[7][0]],mpcorb_format[7][1]);
		boost::algorithm::trim(tmp);

		// Instantiate asteroid.
		::kep_toolbox::epoch epoch(anno,mese,giorno);
		::kep_toolbox::planet target(epoch,elem,ASTRO_MU_SUN,200.0,100.0,110.0);

		// Build the problem.
		pagmo::problem::sample_return prob(target);
		//std::cout << "Computing Asteroid ID: " << j << std::endl;

		for (int k=0;k<n_multistart;++k){
			std::cout << "\tOptimizing for: " << tmp << ", Trial: " << k << std::endl;
			pagmo::archipelago a = pagmo::archipelago(pagmo::topology::rim());
			a.push_back(pagmo::island(prob,algo3,20));
			a.push_back(pagmo::island(prob,algo1,20));
			a.push_back(pagmo::island(prob,algo2,20));
			a.push_back(pagmo::island(prob,algo1,20));
			a.push_back(pagmo::island(prob,algo2,20));
			a.push_back(pagmo::island(prob,algo1,20));
			a.push_back(pagmo::island(prob,algo2,20));

			a.evolve_t(10000);
			a.join();



			std::vector<double> x = a.get_island(0).get_population().champion().x;
			double time = x[4] + x[6] + x[10];
			myfile << "[" << tmp << "] %" << "[" << time << "] " << a.get_island(0).get_population().champion().f << " " << a.get_island(0).get_population().champion().x << std::endl;
		}
	}


	return 0;
}
