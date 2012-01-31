/* ----------------------------------------------------------------------
   LAMMPS - Large-scale Atomic/Molecular Massively Parallel Simulator
   http://lammps.sandia.gov, Sandia National Laboratories
   Steve Plimpton, sjplimp@sandia.gov

   Copyright (2003) Sandia Corporation.  Under the terms of Contract
   DE-AC04-94AL85000 with Sandia Corporation, the U.S. Government retains
   certain rights in this software.  This software is distributed under 
   the GNU General Public License.

   See the README file in the top-level LAMMPS directory.
------------------------------------------------------------------------- */

/* ----------------------------------------------------------------------
   Contributing author: Pieter in 't Veld (SNL)
------------------------------------------------------------------------- */

#include "string.h"
#include "stdlib.h"
#include "math.h"
#include "fix_deform.h"
#include "atom.h"
#include "update.h"
#include "comm.h"
#include "irregular.h"
#include "domain.h"
#include "lattice.h"
#include "force.h"
#include "modify.h"
#include "math_const.h"
#include "kspace.h"
#include "input.h"
#include "variable.h"
#include "error.h"

using namespace LAMMPS_NS;
using namespace FixConst;
using namespace MathConst;

enum{NONE,FINAL,DELTA,SCALE,VEL,ERATE,TRATE,VOLUME,WIGGLE,VARIABLE};
enum{ONE_FROM_ONE,ONE_FROM_TWO,TWO_FROM_ONE};

// same as domain.cpp, fix_nvt_sllod.cpp, compute_temp_deform.cpp

enum{NO_REMAP,X_REMAP,V_REMAP};

/* ---------------------------------------------------------------------- */

FixDeform::FixDeform(LAMMPS *lmp, int narg, char **arg) : Fix(lmp, narg, arg)
{
  if (narg < 4) error->all(FLERR,"Illegal fix deform command");

  box_change = 1;
  no_change_box = 1;

  nevery = atoi(arg[3]);
  if (nevery <= 0) error->all(FLERR,"Illegal fix deform command");

  // set defaults

  set = new Set[6];
  set[0].style = set[1].style = set[2].style = 
    set[3].style = set[4].style = set[5].style = NONE;
  set[0].hstr = set[1].hstr = set[2].hstr = 
    set[3].hstr = set[4].hstr = set[5].hstr = NULL;
  set[0].hratestr = set[1].hratestr = set[2].hratestr = 
    set[3].hratestr = set[4].hratestr = set[5].hratestr = NULL;

  // parse arguments

  triclinic = domain->triclinic;

  int index;
  int iarg = 4;
  while (iarg < narg) {
    if (strcmp(arg[iarg],"x") == 0 || 
	strcmp(arg[iarg],"y") == 0 ||
	strcmp(arg[iarg],"z") == 0) {

      if (strcmp(arg[iarg],"x") == 0) index = 0;
      else if (strcmp(arg[iarg],"y") == 0) index = 1;
      else if (strcmp(arg[iarg],"z") == 0) index = 2;

      if (iarg+2 > narg) error->all(FLERR,"Illegal fix deform command");
      if (strcmp(arg[iarg+1],"final") == 0) {
	if (iarg+4 > narg) error->all(FLERR,"Illegal fix deform command");
	set[index].style = FINAL;
	set[index].flo = atof(arg[iarg+2]);
	set[index].fhi = atof(arg[iarg+3]);
	iarg += 4;
      } else if (strcmp(arg[iarg+1],"delta") == 0) {
	if (iarg+4 > narg) error->all(FLERR,"Illegal fix deform command");
	set[index].style = DELTA;
	set[index].dlo = atof(arg[iarg+2]);
	set[index].dhi = atof(arg[iarg+3]);
	iarg += 4;
      } else if (strcmp(arg[iarg+1],"scale") == 0) {
	if (iarg+3 > narg) error->all(FLERR,"Illegal fix deform command");
	set[index].style = SCALE;
	set[index].scale = atof(arg[iarg+2]);
	iarg += 3;
      } else if (strcmp(arg[iarg+1],"vel") == 0) {
	if (iarg+3 > narg) error->all(FLERR,"Illegal fix deform command");
	set[index].style = VEL;
	set[index].vel = atof(arg[iarg+2]);
	iarg += 3;
      } else if (strcmp(arg[iarg+1],"erate") == 0) {
	if (iarg+3 > narg) error->all(FLERR,"Illegal fix deform command");
	set[index].style = ERATE;
	set[index].rate = atof(arg[iarg+2]);
	iarg += 3;
      } else if (strcmp(arg[iarg+1],"trate") == 0) {
	if (iarg+3 > narg) error->all(FLERR,"Illegal fix deform command");
	set[index].style = TRATE;
	set[index].rate = atof(arg[iarg+2]);
	iarg += 3;
      } else if (strcmp(arg[iarg+1],"volume") == 0) {
	set[index].style = VOLUME;
	iarg += 2;
      } else if (strcmp(arg[iarg+1],"wiggle") == 0) {
	if (iarg+4 > narg) error->all(FLERR,"Illegal fix deform command");
	set[index].style = WIGGLE;
	set[index].amplitude = atof(arg[iarg+2]);
	set[index].tperiod = atof(arg[iarg+3]);
	if (set[index].tperiod <= 0.0) 
	  error->all(FLERR,"Illegal fix deform command");
	iarg += 4;
      } else if (strcmp(arg[iarg+1],"variable") == 0) {
	if (iarg+4 > narg) error->all(FLERR,"Illegal fix deform command");
	set[index].style = VARIABLE;
	if (strstr(arg[iarg+2],"v_") != arg[iarg+2])
	  error->all(FLERR,"Illegal fix deform command");
	if (strstr(arg[iarg+3],"v_") != arg[iarg+3])
	  error->all(FLERR,"Illegal fix deform command");
	delete [] set[index].hstr;
	delete [] set[index].hratestr;
	int n = strlen(&arg[iarg+2][2]) + 1;
	set[index].hstr = new char[n];
	strcpy(set[index].hstr,&arg[iarg+2][2]);
	n = strlen(&arg[iarg+3][2]) + 1;
	set[index].hratestr = new char[n];
	strcpy(set[index].hratestr,&arg[iarg+3][2]);
	iarg += 4;
      } else error->all(FLERR,"Illegal fix deform command");
      
    } else if (strcmp(arg[iarg],"xy") == 0 || 
	       strcmp(arg[iarg],"xz") == 0 ||
	       strcmp(arg[iarg],"yz") == 0) {

      if (triclinic == 0)
	error->all(FLERR,"Fix deform tilt factors require triclinic box");
      if (strcmp(arg[iarg],"xy") == 0) index = 5;
      else if (strcmp(arg[iarg],"xz") == 0) index = 4;
      else if (strcmp(arg[iarg],"yz") == 0) index = 3;

      if (iarg+2 > narg) error->all(FLERR,"Illegal fix deform command");
      if (strcmp(arg[iarg+1],"final") == 0) {
	if (iarg+3 > narg) error->all(FLERR,"Illegal fix deform command");
	set[index].style = FINAL;
	set[index].ftilt = atof(arg[iarg+2]);
	iarg += 3;
      } else if (strcmp(arg[iarg+1],"delta") == 0) {
	if (iarg+3 > narg) error->all(FLERR,"Illegal fix deform command");
	set[index].style = DELTA;
	set[index].dtilt = atof(arg[iarg+2]);
	iarg += 3;
      } else if (strcmp(arg[iarg+1],"vel") == 0) {
	if (iarg+3 > narg) error->all(FLERR,"Illegal fix deform command");
	set[index].style = VEL;
	set[index].vel = atof(arg[iarg+2]);
	iarg += 3;
      } else if (strcmp(arg[iarg+1],"erate") == 0) {
	if (iarg+3 > narg) error->all(FLERR,"Illegal fix deform command");
	set[index].style = ERATE;
	set[index].rate = atof(arg[iarg+2]);
	iarg += 3;
      } else if (strcmp(arg[iarg+1],"trate") == 0) {
	if (iarg+3 > narg) error->all(FLERR,"Illegal fix deform command");
	set[index].style = TRATE;
	set[index].rate = atof(arg[iarg+2]);
	iarg += 3;
      } else if (strcmp(arg[iarg+1],"wiggle") == 0) {
	if (iarg+4 > narg) error->all(FLERR,"Illegal fix deform command");
	set[index].style = WIGGLE;
	set[index].amplitude = atof(arg[iarg+2]);
	set[index].tperiod = atof(arg[iarg+3]);
	if (set[index].tperiod <= 0.0) 
	  error->all(FLERR,"Illegal fix deform command");
	iarg += 4;
      } else if (strcmp(arg[iarg+1],"variable") == 0) {
	if (iarg+4 > narg) error->all(FLERR,"Illegal fix deform command");
	set[index].style = VARIABLE;
	if (strstr(arg[iarg+2],"v_") != arg[iarg+2])
	  error->all(FLERR,"Illegal fix deform command");
	if (strstr(arg[iarg+3],"v_") != arg[iarg+3])
	  error->all(FLERR,"Illegal fix deform command");
	delete [] set[index].hstr;
	delete [] set[index].hratestr;
	int n = strlen(&arg[iarg+2][2]) + 1;
	set[index].hstr = new char[n];
	strcpy(set[index].hstr,&arg[iarg+2][2]);
	n = strlen(&arg[iarg+3][2]) + 1;
	set[index].hratestr = new char[n];
	strcpy(set[index].hratestr,&arg[iarg+3][2]);
	iarg += 4;
      } else error->all(FLERR,"Illegal fix deform command");

    } else break;
  }

  // read options from end of input line
  // no x remap effectively moves atoms within box, so set restart_pbc

  options(narg-iarg,&arg[iarg]);
  if (remapflag != X_REMAP) restart_pbc = 1;

  // setup dimflags used by other classes to check for volume-change conflicts

  for (int i = 0; i < 6; i++)
    if (set[i].style == NONE) dimflag[i] = 0;
    else dimflag[i] = 1;

  if (dimflag[0] || dimflag[1] || dimflag[2]) box_change_size = 1;
  if (dimflag[3] || dimflag[4] || dimflag[5]) box_change_shape = 1;

  // check periodicity

  if ((set[0].style && domain->xperiodic == 0) ||
      (set[1].style && domain->yperiodic == 0) ||
      (set[2].style && domain->zperiodic == 0))
    error->all(FLERR,"Cannot use fix deform on a non-periodic boundary");

  if (set[3].style && domain->zperiodic == 0)
    error->all(FLERR,"Cannot use fix deform on a 2nd non-periodic boundary");
  if (set[4].style && domain->zperiodic == 0)
    error->all(FLERR,"Cannot use fix deform on a 2nd non-periodic boundary");
  if (set[5].style && domain->yperiodic == 0)
    error->all(FLERR,"Cannot use fix deform on a 2nd non-periodic boundary");

  // apply scaling to FINAL,DELTA,VEL,WIGGLE since they have distance/vel units

  int flag = 0;
  for (int i = 0; i < 6; i++)
    if (set[i].style == FINAL || set[i].style == DELTA || 
	set[i].style == VEL || set[i].style == WIGGLE) flag = 1;

  if (flag && scaleflag && domain->lattice == NULL)
    error->all(FLERR,"Use of fix deform with undefined lattice");

  double xscale,yscale,zscale;
  if (flag && scaleflag) {
    xscale = domain->lattice->xlattice;
    yscale = domain->lattice->ylattice;
    zscale = domain->lattice->zlattice;
  }
  else xscale = yscale = zscale = 1.0;

  // for 3,4,5: scaling is in 1st dimension, e.g. x for xz

  double map[6];
  map[0] = xscale; map[1] = yscale; map[2] = zscale;
  map[3] = yscale; map[4] = xscale; map[5] = xscale;

  for (int i = 0; i < 3; i++) {
    if (set[i].style == FINAL) {
      set[i].flo *= map[i];
      set[i].fhi *= map[i];
    } else if (set[i].style == DELTA) {
      set[i].dlo *= map[i];
      set[i].dhi *= map[i];
    } else if (set[i].style == VEL) {
      set[i].vel *= map[i];
    } else if (set[i].style == WIGGLE) {
      set[i].amplitude *= map[i];
    }
  }

  for (int i = 3; i < 6; i++) {
    if (set[i].style == FINAL) set[i].ftilt *= map[i];
    else if (set[i].style == DELTA) set[i].dtilt *= map[i];
    else if (set[i].style == VEL) set[i].vel *= map[i];
    else if (set[i].style == WIGGLE) set[i].amplitude *= map[i];
  }

  // for VOLUME, setup links to other dims
  // fixed, dynamic1, dynamic2

  for (int i = 0; i < 3; i++) {
    if (set[i].style != VOLUME) continue;
    int other1 = (i+1) % 3;
    int other2 = (i+2) % 3;

    if (set[other1].style == NONE) {
      if (set[other2].style == NONE || set[other2].style == VOLUME)
	error->all(FLERR,"Fix deform volume setting is invalid");
      set[i].substyle = ONE_FROM_ONE;
      set[i].fixed = other1;
      set[i].dynamic1 = other2;
    } else if (set[other2].style == NONE) {
      if (set[other1].style == NONE || set[other1].style == VOLUME)
	error->all(FLERR,"Fix deform volume setting is invalid");
      set[i].substyle = ONE_FROM_ONE;
      set[i].fixed = other2;
      set[i].dynamic1 = other1;
    } else if (set[other1].style == VOLUME) {
      if (set[other2].style == NONE || set[other2].style == VOLUME)
	error->all(FLERR,"Fix deform volume setting is invalid");
      set[i].substyle = TWO_FROM_ONE;
      set[i].fixed = other1;
      set[i].dynamic1 = other2;
    } else if (set[other2].style == VOLUME) {
      if (set[other1].style == NONE || set[other1].style == VOLUME)
	error->all(FLERR,"Fix deform volume setting is invalid");
      set[i].substyle = TWO_FROM_ONE;
      set[i].fixed = other2;
      set[i].dynamic1 = other1;
    } else {
      set[i].substyle = ONE_FROM_TWO;
      set[i].dynamic1 = other1;
      set[i].dynamic2 = other2;
    }
  }

  // set initial values at time fix deform is issued

  for (int i = 0; i < 3; i++) {
    set[i].lo_initial = domain->boxlo[i];
    set[i].hi_initial = domain->boxhi[i];
    set[i].vol_initial = domain->xprd * domain->yprd * domain->zprd;
  }
  for (int i = 3; i < 6; i++) {
    if (i == 5) set[i].tilt_initial = domain->xy;
    else if (i == 4) set[i].tilt_initial = domain->xz;
    else if (i == 3) set[i].tilt_initial = domain->yz;
  }

  // reneighboring only forced if flips will occur due to shape changes

  if (set[3].style || set[4].style || set[5].style) force_reneighbor = 1;
  next_reneighbor = -1;

  nrigid = 0;
  rfix = NULL;
  flip = 0;
  
  if (force_reneighbor) irregular = new Irregular(lmp);
  else irregular = NULL;

  TWOPI = 2.0*MY_PI;
}

/* ---------------------------------------------------------------------- */

FixDeform::~FixDeform()
{
  for (int i = 0; i < 6; i++) {
    delete [] set[i].hstr;
    delete [] set[i].hratestr;
  }
  delete [] set;
  delete [] rfix;

  delete irregular;

  // reset domain's h_rate = 0.0, since this fix may have made it non-zero

  double *h_rate = domain->h_rate;
  double *h_ratelo = domain->h_ratelo;

  h_rate[0] = h_rate[1] = h_rate[2] = 
    h_rate[3] = h_rate[4] = h_rate[5] = 0.0;
  h_ratelo[0] = h_ratelo[1] = h_ratelo[2] = 0.0;
}

/* ---------------------------------------------------------------------- */

int FixDeform::setmask()
{
  int mask = 0;
  if (force_reneighbor) mask |= PRE_EXCHANGE;
  mask |= END_OF_STEP;
  return mask;
}

/* ---------------------------------------------------------------------- */

void FixDeform::init()
{
  // error if more than one fix deform
  // domain, fix nvt/sllod, compute temp/deform only work on single h_rate

  int count = 0;
  for (int i = 0; i < modify->nfix; i++)
    if (strcmp(modify->fix[i]->style,"deform") == 0) count++;
  if (count > 1) error->all(FLERR,"More than one fix deform");

  // Kspace setting

  if (force->kspace) kspace_flag = 1;
  else kspace_flag = 0;

  // elapsed time for entire simulation, including multiple runs if defined

  double delt = (update->endstep - update->beginstep) * update->dt;

  // check variables for VARIABLE style

  for (int i = 0; i < 6; i++) {
    if (set[i].style != VARIABLE) continue;
    set[i].hvar = input->variable->find(set[i].hstr);
    if (set[i].hvar < 0) 
      error->all(FLERR,"Variable name for fix deform does not exist");
    if (!input->variable->equalstyle(set[i].hvar))
      error->all(FLERR,"Variable for fix deform is invalid style");
    set[i].hratevar = input->variable->find(set[i].hratestr);
    if (set[i].hratevar < 0) 
      error->all(FLERR,"Variable name for fix deform does not exist");
    if (!input->variable->equalstyle(set[i].hratevar))
      error->all(FLERR,"Variable for fix deform is invalid style");
  }

  // set start/stop values for box size and shape
  // if single run, start is current values
  // if multiple runs enabled via run start/stop settings,
  //   start is value when fix deform was issued
  // if NONE, no need to set

  for (int i = 0; i < 3; i++) {
    if (update->firststep == update->beginstep) {
      set[i].lo_start = domain->boxlo[i];
      set[i].hi_start = domain->boxhi[i];
      set[i].vol_start = domain->xprd * domain->yprd * domain->zprd;
    } else {
      set[i].lo_start = set[i].lo_initial;
      set[i].hi_start = set[i].hi_initial;
      set[i].vol_start = set[i].vol_initial;
    }

    if (set[i].style == FINAL) {
      set[i].lo_stop = set[i].flo;
      set[i].hi_stop = set[i].fhi;
    } else if (set[i].style == DELTA) {
      set[i].lo_stop = set[i].lo_start + set[i].dlo;
      set[i].hi_stop = set[i].hi_start + set[i].dhi;
    } else if (set[i].style == SCALE) {
      set[i].lo_stop = 0.5*(set[i].lo_start+set[i].hi_start) - 
	0.5*set[i].scale*(set[i].hi_start-set[i].lo_start);
      set[i].hi_stop = 0.5*(set[i].lo_start+set[i].hi_start) + 
	0.5*set[i].scale*(set[i].hi_start-set[i].lo_start);
    } else if (set[i].style == VEL) {
      set[i].lo_stop = set[i].lo_start - 0.5*delt*set[i].vel;
      set[i].hi_stop = set[i].hi_start + 0.5*delt*set[i].vel;
    } else if (set[i].style == ERATE) {
      set[i].lo_stop = set[i].lo_start - 
	0.5*delt*set[i].rate * (set[i].hi_start-set[i].lo_start);
      set[i].hi_stop = set[i].hi_start + 
	0.5*delt*set[i].rate * (set[i].hi_start-set[i].lo_start);
      if (set[i].hi_stop <= set[i].lo_stop)
	error->all(FLERR,"Final box dimension due to fix deform is < 0.0");
    } else if (set[i].style == TRATE) {
      set[i].lo_stop = 0.5*(set[i].lo_start+set[i].hi_start) - 
	0.5*((set[i].hi_start-set[i].lo_start) * exp(set[i].rate*delt));
      set[i].hi_stop = 0.5*(set[i].lo_start+set[i].hi_start) + 
	0.5*((set[i].hi_start-set[i].lo_start) * exp(set[i].rate*delt));
    } else if (set[i].style == WIGGLE) {
      set[i].lo_stop = set[i].lo_start -
	0.5*set[i].amplitude * sin(TWOPI*delt/set[i].tperiod);
      set[i].hi_stop = set[i].hi_start +
	0.5*set[i].amplitude * sin(TWOPI*delt/set[i].tperiod);
    } else if (set[i].style == VARIABLE) {
      bigint holdstep = update->ntimestep;
      update->ntimestep = update->endstep;
      double delta = input->variable->compute_equal(set[i].hvar);
      update->ntimestep = holdstep;
      set[i].lo_stop = set[i].lo_start - 0.5*delta;
      set[i].hi_stop = set[i].hi_start + 0.5*delta;
    }
  }

  for (int i = 3; i < 6; i++) {
    if (update->firststep == update->beginstep) {
      if (i == 5) set[i].tilt_start = domain->xy;
      else if (i == 4) set[i].tilt_start = domain->xz;
      else if (i == 3) set[i].tilt_start = domain->yz;
    } else set[i].tilt_start = set[i].tilt_initial;

    if (set[i].style == FINAL) {
      set[i].tilt_stop = set[i].ftilt;
    } else if (set[i].style == DELTA) {
      set[i].tilt_stop = set[i].tilt_start + set[i].dtilt;
    } else if (set[i].style == VEL) {
      set[i].tilt_stop = set[i].tilt_start + delt*set[i].vel;
    } else if (set[i].style == ERATE) {
      if (i == 3) set[i].tilt_stop = set[i].tilt_start + 
		    delt*set[i].rate * (set[2].hi_start-set[2].lo_start);
      if (i == 4) set[i].tilt_stop = set[i].tilt_start + 
		    delt*set[i].rate * (set[2].hi_start-set[2].lo_start);
      if (i == 5) set[i].tilt_stop = set[i].tilt_start + 
		    delt*set[i].rate * (set[1].hi_start-set[1].lo_start);
    } else if (set[i].style == TRATE) {
      set[i].tilt_stop = set[i].tilt_start * exp(set[i].rate*delt);
    } else if (set[i].style == WIGGLE) {
      set[i].tilt_stop = set[i].tilt_start +
	set[i].amplitude * sin(TWOPI*delt/set[i].tperiod);

      // compute min/max for WIGGLE = extrema tilt factor will ever reach

      if (set[i].amplitude >= 0.0) {
	if (delt < 0.25*set[i].tperiod) {
	  set[i].tilt_min = set[i].tilt_start;
	  set[i].tilt_max = set[i].tilt_start + 
	    set[i].amplitude*sin(TWOPI*delt/set[i].tperiod);
	} else if (delt < 0.5*set[i].tperiod) {
	  set[i].tilt_min = set[i].tilt_start;
	  set[i].tilt_max = set[i].tilt_start + set[i].amplitude;
	} else if (delt < 0.75*set[i].tperiod) {
	  set[i].tilt_min = set[i].tilt_start - 
	    set[i].amplitude*sin(TWOPI*delt/set[i].tperiod);
	  set[i].tilt_max = set[i].tilt_start + set[i].amplitude;
	} else {
	  set[i].tilt_min = set[i].tilt_start - set[i].amplitude;
	  set[i].tilt_max = set[i].tilt_start + set[i].amplitude;
	}
      } else {
	if (delt < 0.25*set[i].tperiod) {
	  set[i].tilt_min = set[i].tilt_start - 
	    set[i].amplitude*sin(TWOPI*delt/set[i].tperiod);
	  set[i].tilt_max = set[i].tilt_start;
	} else if (delt < 0.5*set[i].tperiod) {
	  set[i].tilt_min = set[i].tilt_start - set[i].amplitude;
	  set[i].tilt_max = set[i].tilt_start;
	} else if (delt < 0.75*set[i].tperiod) {
	  set[i].tilt_min = set[i].tilt_start - set[i].amplitude;
	  set[i].tilt_max = set[i].tilt_start + 
	    set[i].amplitude*sin(TWOPI*delt/set[i].tperiod);
	} else {
	  set[i].tilt_min = set[i].tilt_start - set[i].amplitude;
	  set[i].tilt_max = set[i].tilt_start + set[i].amplitude;
	}
      }

    } else if (set[i].style == VARIABLE) {
      bigint holdstep = update->ntimestep;
      update->ntimestep = update->endstep;
      double delta_tilt = input->variable->compute_equal(set[i].hvar);
      update->ntimestep = holdstep;
      set[i].tilt_stop = set[i].tilt_start + delta_tilt;
    }
  }

  // if using tilt TRATE, then initial tilt must be non-zero

  for (int i = 3; i < 6; i++)
    if (set[i].style == TRATE && set[i].tilt_start == 0.0)
      error->all(FLERR,"Cannot use fix deform trate on a box with zero tilt");

  // if yz changes and will cause box flip, then xy cannot be changing
  // NOTE: is this comment correct, not what is tested below?
  // test for WIGGLE is on min/max oscillation limit, not tilt_stop
  // test for VARIABLE is error, since no way to calculate min/max limits
  // this is b/c the flips would induce continuous changes in xz
  //   in order to keep the edge vectors of the flipped shape matrix
  //   an integer combination of the edge vectors of the unflipped shape matrix

  if (set[3].style && set[5].style) {
    int flag = 0;
    double lo,hi;
    if (set[3].style == WIGGLE) {
      lo = set[3].tilt_min;
      hi = set[3].tilt_max;
    } else lo = hi = set[3].tilt_stop;
    if (set[3].style == VARIABLE)
      error->all(FLERR,"Fix deform is changing yz with xy variable");
    // NOTE is this preceding error test and message correct?
    // NOTE is this test on set[1] correct?  which is y dim?
    if (lo < -0.5*(set[1].hi_start-set[1].lo_start) ||
	hi > 0.5*(set[1].hi_start-set[1].lo_start)) flag = 1;
    if (set[1].style) {
      if (lo < -0.5*(set[1].hi_stop-set[1].lo_stop) ||
	  hi > 0.5*(set[1].hi_stop-set[1].lo_stop)) flag = 1;
    }
    if (flag)
      error->all(FLERR,
		 "Fix deform is changing yz by too much with changing xy");
  }

  // set domain->h_rate values for use by domain and other fixes/computes
  // initialize all rates to 0.0
  // cannot set here for TRATE,VOLUME,WIGGLE,VARIABLE since not constant

  h_rate = domain->h_rate;
  h_ratelo = domain->h_ratelo;

  for (int i = 0; i < 3; i++) {
    h_rate[i] = h_ratelo[i] = 0.0;
    if (set[i].style == FINAL || set[i].style == DELTA ||
	set[i].style == SCALE || set[i].style == VEL || 
	set[i].style == ERATE) {
      double dlo_dt,dhi_dt;
      if (delt != 0.0) {
	dlo_dt = (set[i].lo_stop - set[i].lo_start) / delt;
	dhi_dt = (set[i].hi_stop - set[i].hi_start) / delt;
      } else dlo_dt = dhi_dt = 0.0;
      h_rate[i] = dhi_dt - dlo_dt;
      h_ratelo[i] = dlo_dt;
    }
  }

  for (int i = 3; i < 6; i++) {
    h_rate[i] = 0.0;
    if (set[i].style == FINAL || set[i].style == DELTA || 
	set[i].style == VEL || set[i].style == ERATE) {
      if (delt != 0.0)
	h_rate[i] = (set[i].tilt_stop - set[i].tilt_start) / delt;
      else h_rate[i] = 0.0;
    }
  }    

  // detect if any rigid fixes exist so rigid bodies can be rescaled
  // rfix[] = indices to each fix rigid

  delete [] rfix;
  nrigid = 0;
  rfix = NULL;

  for (int i = 0; i < modify->nfix; i++)
    if (modify->fix[i]->rigid_flag) nrigid++;
  if (nrigid) {
    rfix = new int[nrigid];
    nrigid = 0;
    for (int i = 0; i < modify->nfix; i++)
      if (modify->fix[i]->rigid_flag) rfix[nrigid++] = i;
  }
}

/* ----------------------------------------------------------------------
  box flipped on previous step
  perform irregular comm to migrate atoms to new procs
  reset box tilts for flipped config and create new box in domain
  remap to put far-away atoms back into new box
  perform irregular on atoms in lamda coords to get atoms to new procs
  force reneighboring on next timestep
------------------------------------------------------------------------- */

void FixDeform::pre_exchange()
{
  if (flip == 0) return;
  
  domain->yz = set[3].tilt_target = set[3].tilt_flip;
  domain->xz = set[4].tilt_target = set[4].tilt_flip;
  domain->xy = set[5].tilt_target = set[5].tilt_flip;
  domain->set_global_box();
  domain->set_local_box();

  double **x = atom->x;
  int *image = atom->image;
  int nlocal = atom->nlocal;
  for (int i = 0; i < nlocal; i++) domain->remap(x[i],image[i]);

  domain->x2lamda(atom->nlocal);
  irregular->migrate_atoms();
  domain->lamda2x(atom->nlocal);

  flip = 0;
}

/* ---------------------------------------------------------------------- */

void FixDeform::end_of_step()
{
  int i;

  double delta = update->ntimestep - update->beginstep;
  delta /= update->endstep - update->beginstep;

  // set new box size
  // for TRATE, set target directly based on current time, also set h_rate
  // for WIGGLE, set target directly based on current time, also set h_rate
  // for VARIABLE, set target directly based on current time, also set h_rate
  // for NONE, target is current box size
  // for others except VOLUME, target is linear value between start and stop

  for (i = 0; i < 3; i++) {
    if (set[i].style == TRATE) {
      double delt = (update->ntimestep - update->beginstep) * update->dt;
      set[i].lo_target = 0.5*(set[i].lo_start+set[i].hi_start) - 
	0.5*((set[i].hi_start-set[i].lo_start) * exp(set[i].rate*delt));
      set[i].hi_target = 0.5*(set[i].lo_start+set[i].hi_start) + 
	0.5*((set[i].hi_start-set[i].lo_start) * exp(set[i].rate*delt));
      h_rate[i] = set[i].rate * domain->h[i];
      h_ratelo[i] = -0.5*h_rate[i];
    } else if (set[i].style == WIGGLE) {
      double delt = (update->ntimestep - update->beginstep) * update->dt;
      set[i].lo_target = set[i].lo_start -
	0.5*set[i].amplitude * sin(TWOPI*delt/set[i].tperiod);
      set[i].hi_target = set[i].hi_start +
	0.5*set[i].amplitude * sin(TWOPI*delt/set[i].tperiod);
      h_rate[i] = TWOPI/set[i].tperiod * set[i].amplitude * 
	cos(TWOPI*delt/set[i].tperiod);
      h_ratelo[i] = -0.5*h_rate[i];
    } else if (set[i].style == VARIABLE) {
      // NOTE: worry about clearstep, addstep, maybe set on
      // outside of loop with global varflag
      double delta = input->variable->compute_equal(set[i].hvar);
      set[i].lo_target = set[i].lo_start - 0.5*delta;
      set[i].hi_target = set[i].hi_start + 0.5*delta;
      h_rate[i] = input->variable->compute_equal(set[i].hratevar);
      h_ratelo[i] = -0.5*h_rate[i];
    } else if (set[i].style == NONE) {
      set[i].lo_target = domain->boxlo[i];
      set[i].hi_target = domain->boxhi[i];
    } else if (set[i].style != VOLUME) {
      set[i].lo_target = set[i].lo_start + 
	delta*(set[i].lo_stop - set[i].lo_start);
      set[i].hi_target = set[i].hi_start + 
	delta*(set[i].hi_stop - set[i].hi_start);
    }
  }

  // set new box size for VOLUME dims that are linked to other dims
  // NOTE: still need to set h_rate for these dims

  for (int i = 0; i < 3; i++) {
    if (set[i].style != VOLUME) continue;

    if (set[i].substyle == ONE_FROM_ONE) {
      set[i].lo_target = 0.5*(set[i].lo_start+set[i].hi_start) -
	0.5*(set[i].vol_start /
	     (set[set[i].dynamic1].hi_target -
	      set[set[i].dynamic1].lo_target) /
	     (set[set[i].fixed].hi_start-set[set[i].fixed].lo_start));
      set[i].hi_target = 0.5*(set[i].lo_start+set[i].hi_start) +
	0.5*(set[i].vol_start /
	     (set[set[i].dynamic1].hi_target -
	      set[set[i].dynamic1].lo_target) /
	     (set[set[i].fixed].hi_start-set[set[i].fixed].lo_start));

    } else if (set[i].substyle == ONE_FROM_TWO) {
      set[i].lo_target = 0.5*(set[i].lo_start+set[i].hi_start) -
	0.5*(set[i].vol_start /
	     (set[set[i].dynamic1].hi_target - 
	      set[set[i].dynamic1].lo_target) /
	     (set[set[i].dynamic2].hi_target - 
	      set[set[i].dynamic2].lo_target));
      set[i].hi_target = 0.5*(set[i].lo_start+set[i].hi_start) +
	0.5*(set[i].vol_start /
	     (set[set[i].dynamic1].hi_target -
	      set[set[i].dynamic1].lo_target) /
	     (set[set[i].dynamic2].hi_target - 
	      set[set[i].dynamic2].lo_target));
      
    } else if (set[i].substyle == TWO_FROM_ONE) {
      set[i].lo_target = 0.5*(set[i].lo_start+set[i].hi_start) -
	0.5*sqrt(set[i].vol_start /
		 (set[set[i].dynamic1].hi_target - 
		  set[set[i].dynamic1].lo_target) /
		 (set[set[i].fixed].hi_start - 
		  set[set[i].fixed].lo_start) *
		 (set[i].hi_start - set[i].lo_start));
      set[i].hi_target = 0.5*(set[i].lo_start+set[i].hi_start) +
	0.5*sqrt(set[i].vol_start /
		 (set[set[i].dynamic1].hi_target - 
		  set[set[i].dynamic1].lo_target) /
		 (set[set[i].fixed].hi_start - 
		  set[set[i].fixed].lo_start) *
		 (set[i].hi_start - set[i].lo_start));
    }
  }

  // for triclinic, set new box shape
  // for TRATE, set target directly based on current time. also set h_rate
  // for WIGGLE, set target directly based on current time. also set h_rate
  // for VARIABLE, set target directly based on current time. also set h_rate
  // for NONE, target is current tilt
  // for other styles, target is linear value between start and stop values

  if (triclinic) {
    double *h = domain->h;

    for (i = 3; i < 6; i++) {
      if (set[i].style == TRATE) {
	double delt = (update->ntimestep - update->beginstep) * update->dt;
	set[i].tilt_target = set[i].tilt_start * exp(set[i].rate*delt);
	h_rate[i] = set[i].rate * domain->h[i];
      } else if (set[i].style == WIGGLE) {
	double delt = (update->ntimestep - update->beginstep) * update->dt;
	set[i].tilt_target = set[i].tilt_start +
	  set[i].amplitude * sin(TWOPI*delt/set[i].tperiod);
	h_rate[i] = TWOPI/set[i].tperiod * set[i].amplitude * 
	  cos(TWOPI*delt/set[i].tperiod);
      } else if (set[i].style == VARIABLE) {
	double delta_tilt = input->variable->compute_equal(set[i].hvar);
	set[i].tilt_target = set[i].tilt_start + delta_tilt;
	h_rate[i] = input->variable->compute_equal(set[i].hratevar);
      } else if (set[i].style == NONE) {
	if (i == 5) set[i].tilt_target = domain->xy;
	else if (i == 4) set[i].tilt_target = domain->xz;
	else if (i == 3) set[i].tilt_target = domain->yz;
      } else {
	set[i].tilt_target = set[i].tilt_start + 
	  delta*(set[i].tilt_stop - set[i].tilt_start);
      }

      // tilt_target can be large positive or large negative value
      // add/subtract box lengths until tilt_target is closest to current value

      int idenom;
      if (i == 5) idenom = 0;
      else if (i == 4) idenom = 0;
      else if (i == 3) idenom = 1;
      double denom = set[idenom].hi_target - set[idenom].lo_target;

      double current = h[i]/h[idenom];
      while (set[i].tilt_target/denom - current > 0.0)
	set[i].tilt_target -= denom;
      while (set[i].tilt_target/denom - current < 0.0)
	set[i].tilt_target += denom;
      if (fabs(set[i].tilt_target/denom - 1.0 - current) < 
	  fabs(set[i].tilt_target/denom - current))
	set[i].tilt_target -= denom;
    }
  }

  // if any tilt targets exceed bounds, set flip flag and new tilt_flip values
  // flip will be performed on next timestep before reneighboring
  // when yz flips and xy is non-zero, xz must also change
  // this is to keep the edge vectors of the flipped shape matrix
  //   an integer combination of the edge vectors of the unflipped shape matrix

  if (triclinic) {
    double xprd = set[0].hi_target - set[0].lo_target;
    double yprd = set[1].hi_target - set[1].lo_target;
    if (set[3].tilt_target < -0.5*yprd || set[3].tilt_target > 0.5*yprd ||
	set[4].tilt_target < -0.5*xprd || set[4].tilt_target > 0.5*xprd ||
	set[5].tilt_target < -0.5*xprd || set[5].tilt_target > 0.5*xprd) {

      flip = 1;
      next_reneighbor = update->ntimestep + 1;
      set[3].tilt_flip = set[3].tilt_target;
      set[4].tilt_flip = set[4].tilt_target;
      set[5].tilt_flip = set[5].tilt_target;

      if (set[3].tilt_flip < -0.5*yprd) {
	set[3].tilt_flip += yprd;
	set[4].tilt_flip += set[5].tilt_flip;
      } else if (set[3].tilt_flip >= 0.5*yprd) {
	set[3].tilt_flip -= yprd;
	set[4].tilt_flip -= set[5].tilt_flip;
      }
      if (set[4].tilt_flip < -0.5*xprd) set[4].tilt_flip += xprd;
      if (set[4].tilt_flip > 0.5*xprd) set[4].tilt_flip -= xprd;
      if (set[5].tilt_flip < -0.5*xprd) set[5].tilt_flip += xprd;
      if (set[5].tilt_flip > 0.5*xprd) set[5].tilt_flip -= xprd;
    }
  }

  // convert atoms and rigid bodies to lamda coords

  if (remapflag == X_REMAP) {
    double **x = atom->x;
    int *mask = atom->mask;
    int nlocal = atom->nlocal;

    for (i = 0; i < nlocal; i++)
      if (mask[i] & groupbit)
	domain->x2lamda(x[i],x[i]);

    if (nrigid)
      for (i = 0; i < nrigid; i++)
	modify->fix[rfix[i]]->deform(0);
  }

  // reset global and local box to new size/shape
  // only if deform fix is controlling the dimension

  if (set[0].style) {
    domain->boxlo[0] = set[0].lo_target;
    domain->boxhi[0] = set[0].hi_target;
  }
  if (set[1].style) {
    domain->boxlo[1] = set[1].lo_target;
    domain->boxhi[1] = set[1].hi_target;
  }
  if (set[2].style) {
    domain->boxlo[2] = set[2].lo_target;
    domain->boxhi[2] = set[2].hi_target;
  }
  if (triclinic) {
    if (set[3].style) domain->yz = set[3].tilt_target;
    if (set[4].style) domain->xz = set[4].tilt_target;
    if (set[5].style) domain->xy = set[5].tilt_target;
  }

  domain->set_global_box();
  domain->set_local_box();

  // convert atoms and rigid bodies back to box coords

  if (remapflag == X_REMAP) {
    double **x = atom->x;
    int *mask = atom->mask;
    int nlocal = atom->nlocal;

    for (i = 0; i < nlocal; i++)
      if (mask[i] & groupbit)
	domain->lamda2x(x[i],x[i]);

    if (nrigid)
      for (i = 0; i < nrigid; i++)
	modify->fix[rfix[i]]->deform(1);
  }

  // redo KSpace coeffs since box has changed

  if (kspace_flag) force->kspace->setup();
}

/* ---------------------------------------------------------------------- */

void FixDeform::options(int narg, char **arg)
{
  if (narg < 0) error->all(FLERR,"Illegal fix deform command");

  remapflag = X_REMAP;
  scaleflag = 1;

  int iarg = 0;
  while (iarg < narg) {
    if (strcmp(arg[iarg],"remap") == 0) {
      if (iarg+2 > narg) error->all(FLERR,"Illegal fix deform command");
      if (strcmp(arg[iarg+1],"x") == 0) remapflag = X_REMAP;
      else if (strcmp(arg[iarg+1],"v") == 0) remapflag = V_REMAP;
      else if (strcmp(arg[iarg+1],"none") == 0) remapflag = NO_REMAP;
      else error->all(FLERR,"Illegal fix deform command");
      iarg += 2;
    } else if (strcmp(arg[iarg],"units") == 0) {
      if (iarg+2 > narg) error->all(FLERR,"Illegal fix deform command");
      if (strcmp(arg[iarg+1],"box") == 0) scaleflag = 0;
      else if (strcmp(arg[iarg+1],"lattice") == 0) scaleflag = 1;
      else error->all(FLERR,"Illegal fix deform command");
      iarg += 2;
    } else error->all(FLERR,"Illegal fix deform command");
  }
}
