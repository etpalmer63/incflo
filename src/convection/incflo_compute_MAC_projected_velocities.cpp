#include <Godunov.H>
#include <MOL.H>
#include <incflo.H>

#include <AMReX_MultiFabUtil.H>
#include <AMReX_MacProjector.H>

using namespace amrex;

void
incflo::compute_MAC_projected_velocities (
                                 Vector<MultiFab const*> const& vel,
                                 AMREX_D_DECL(Vector<MultiFab*> const& u_mac,
                                              Vector<MultiFab*> const& v_mac,
                                              Vector<MultiFab*> const& w_mac),
                                 AMREX_D_DECL(Vector<MultiFab*> const& inv_rho_x,
                                              Vector<MultiFab*> const& inv_rho_y,
                                              Vector<MultiFab*> const& inv_rho_z),
                                 Vector<MultiFab*      > const& vel_forces,
                                 Real time)
{
    BL_PROFILE("incflo::compute_MAC_projected_velocities()");
    Real l_dt = m_dt;

    auto mac_phi = get_mac_phi();

    for (int lev = 0; lev <= finest_level; ++lev) {

        mac_phi[lev]->FillBoundary(geom[lev].periodicity());

        // Predict normal velocity to faces -- note that the {u_mac, v_mac, w_mac}
        //    returned from this call are on face CENTROIDS

        if (m_use_godunov) {

            godunov::predict_godunov(lev, time, 
                                     AMREX_D_DECL(*u_mac[lev], *v_mac[lev], *w_mac[lev]), 
                                     *mac_phi[lev], *vel[lev], *vel_forces[lev], 
                                     AMREX_D_DECL(*inv_rho_x[lev], *inv_rho_y[lev], *inv_rho_z[lev]), 
                                     get_velocity_bcrec(), get_velocity_bcrec_device_ptr(), 
                                     Geom(), l_dt, m_godunov_ppm, m_godunov_use_forces_in_trans,
                                     m_use_mac_phi_in_godunov);
        } else {

#ifdef AMREX_USE_EB
            const EBFArrayBoxFactory* ebfact = &EBFactory(lev);
            mol::predict_vels_on_faces(lev, AMREX_D_DECL(*u_mac[lev], *v_mac[lev], *w_mac[lev]), *vel[lev],
                                       get_velocity_bcrec(), get_velocity_bcrec_device_ptr(), 
                                       ebfact,
#else
            mol::predict_vels_on_faces(lev, AMREX_D_DECL(*u_mac[lev], *v_mac[lev], *w_mac[lev]), *vel[lev],
                                       get_velocity_bcrec(), get_velocity_bcrec_device_ptr(), 
#endif
                                       Geom()); 
        }
    }

    Vector<Array<MultiFab*,AMREX_SPACEDIM> > mac_vec(finest_level+1);
    Vector<Array<MultiFab const*,AMREX_SPACEDIM> > inv_rho(finest_level+1);
    for (int lev=0; lev <= finest_level; ++lev)
    {
        AMREX_D_TERM(mac_vec[lev][0] = u_mac[lev];,
                     mac_vec[lev][1] = v_mac[lev];,
                     mac_vec[lev][2] = w_mac[lev];);
        AMREX_D_TERM(inv_rho[lev][0] = inv_rho_x[lev];,
                     inv_rho[lev][1] = inv_rho_y[lev];,
                     inv_rho[lev][2] = inv_rho_z[lev];);
    }

    //
    // If we want to set max_coarsening_level we have to send it in to the constructor
    //
    LPInfo lp_info;
    lp_info.setMaxCoarseningLevel(m_mac_mg_max_coarsening_level);

    if (macproj->needInitialization()) 
        macproj->initProjector(lp_info, inv_rho);
    else
        macproj->updateBeta(inv_rho);

    macproj->setUMAC(mac_vec);

    macproj->setDomainBC(get_projection_bc(Orientation::low), get_projection_bc(Orientation::high));

    if (m_verbose > 2) amrex::Print() << "MAC Projection:\n";
    //
    // Perform MAC projection
    //
    if (m_use_mac_phi_in_godunov)
    {
        for (int lev=0; lev <= finest_level; ++lev)
            mac_phi[lev]->mult(m_dt/2.,0,1,1);

        macproj->project(mac_phi,m_mac_mg_rtol,m_mac_mg_atol);

        for (int lev=0; lev <= finest_level; ++lev)
            mac_phi[lev]->mult(2./m_dt,0,1,1);
    } else {
        macproj->project(m_mac_mg_rtol,m_mac_mg_atol);
    }
}