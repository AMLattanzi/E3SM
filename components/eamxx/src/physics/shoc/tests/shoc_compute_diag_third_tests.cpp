#include "catch2/catch.hpp"

#include "shoc_unit_tests_common.hpp"
#include "shoc_functions.hpp"
#include "shoc_test_data.hpp"
#include "physics/share/physics_constants.hpp"
#include "share/eamxx_types.hpp"
#include "share/util/eamxx_setup_random_test.hpp"

#include "ekat/ekat_pack.hpp"
#include "ekat/util/ekat_arch.hpp"
#include "ekat/kokkos/ekat_kokkos_utils.hpp"

#include <algorithm>
#include <array>
#include <random>
#include <thread>

namespace scream {
namespace shoc {
namespace unit_test {

template <typename D>
struct UnitWrap::UnitTest<D>::TestShocCompDiagThird : public UnitWrap::UnitTest<D>::Base {

  void run_property()
  {
    static constexpr Int shcol    = 2;
    static constexpr Int nlev     = 5;
    static constexpr Int nlevi    = nlev+1;

    // Tests for the SHOC function:
    //   compute_diag_third_shoc_moment

    // Mid-level function for w3

    // convective boundary layer test
    //  Feed in profiles that are representative of a convective boundary
    //  layer and verify that results are as expected i.e. boundary points
    //  are good and there is at least one point that has a positive w3 value.
    //  In addition, the profiles being fed in below are completely reasonable
    //  so will also verify that w3 falls within some reasonable bounds.

    // IN ADDITION will feed subsequent columns values with increasing
    //  scalar fluxes.  The w3 term is proportional to these, thus we need to
    //  verify that as the scalar fluxes increase that the absolute value of
    //  w3 increases, given all other inputs are the same.

    // Define vertical velocity second moment [m2/s2]
    static constexpr Real w_sec_zi[nlevi] = {0.2, 0.3, 0.5, 0.4, 0.3, 0.1};
    // Define potential temperature second moment [K2]
    static constexpr Real thl_sec[nlevi] = {0.5, 0.9, 1.2, 0.8, 0.4, 0.3};
    // Define vertical flux of temperature [K m/s]
    static constexpr Real wthl_sec[nlevi] = {0.003, -0.03, -0.04, -0.01, 0.01, 0.03};
    // Define the heights on the zi grid [m]
    static constexpr Real zi_grid[nlevi] = {9000, 5000, 1500, 900, 500, 0};
    // Define the return to isotropy timescale [s]
    static constexpr Real isotropy_zi[nlevi] = {2000, 3000, 5000, 2000, 1000, 500};
    // Define the brunt vaisalla frequency
    static constexpr Real brunt_zi[nlevi] = {4e-5, 3e-5, 3e-5, 2e-5, 2e-5, -1e-5};
    // Define the potential temperature on zi grid [K]
    static constexpr Real thetal_zi[nlevi] = {330, 325, 320, 310, 300, 301};

    // Define TKE [m2/s2], compute from w_sec
    Real tke[nlev];

    // Grid stuff to compute based on zi_grid
    Real zt_grid[nlev];
    Real dz_zt[nlev];
    Real dz_zi[nlevi];
    Real w_sec[nlev];
    // Compute heights on midpoint grid
    for(Int n = 0; n < nlev; ++n) {
      zt_grid[n] = 0.5*(zi_grid[n]+zi_grid[n+1]);
      w_sec[n] = 0.5*(w_sec_zi[n]+w_sec_zi[n+1]);
      tke[n] = 1.5*w_sec[n];
      dz_zt[n] = zi_grid[n] - zi_grid[n+1];
      if (n == 0){
        dz_zi[n] = 0;
      }
      else{
        dz_zi[n] = zt_grid[n-1] - zt_grid[n];
      }
    }
    // set upper condition for dz_zi
    dz_zi[nlevi-1] = zt_grid[nlev-1];

    // Default SHOC formulation, not 1.5 TKE closure assumptions
    const bool shoc_1p5tke = false;

    // Initialize data structure for bridging to F90
    ComputeDiagThirdShocMomentData SDS(shcol, nlev, nlevi, shoc_1p5tke);

    // Test that the inputs are reasonable.
    // For this test shcol MUST be at least 2
    REQUIRE( (SDS.shcol == shcol && SDS.nlev == nlev && SDS.nlevi == nlevi) );
    REQUIRE(SDS.nlevi == SDS.nlev+1);
    REQUIRE(shcol > 1);

    // Load up the new data
    for(Int s = 0; s < shcol; ++s) {
      // Fill in test data on zt_grid.
      for(Int n = 0; n < nlev; ++n) {
        const auto offset = n + s * nlev;

        SDS.w_sec[offset] = w_sec[n];
        SDS.dz_zt[offset] = dz_zt[n];
        SDS.tke[offset] = tke[n];

      }

      // Fill in test data on zi_grid.
      for(Int n = 0; n < nlevi; ++n) {
        const auto offset = n + s * nlevi;

        SDS.dz_zi[offset] = dz_zi[n];
        SDS.thl_sec[offset] = (s+1)*thl_sec[n];
        SDS.wthl_sec[offset] = wthl_sec[n];

        SDS.w_sec_zi[offset] = w_sec_zi[n];
        SDS.isotropy_zi[offset] = isotropy_zi[n];
        SDS.brunt_zi[offset] = brunt_zi[n];
        SDS.thetal_zi[offset] = thetal_zi[n];

      }
    }

    // Check that the inputs make sense
    // Load up the new data
    for(Int s = 0; s < shcol; ++s) {
      // Fill in test data on zt_grid.
      for(Int n = 0; n < nlev; ++n) {
        const auto offset = n + s * nlev;

        REQUIRE(SDS.w_sec[offset] >= 0);
        REQUIRE(SDS.dz_zt[offset] > 0);
        REQUIRE(SDS.tke[offset] > 0);
      }

      for(Int n = 0; n < nlevi; ++n) {
        const auto offset = n + s * nlevi;

        REQUIRE(SDS.dz_zi[offset] >= 0);
        REQUIRE(SDS.thl_sec[offset] >= 0);
        REQUIRE(SDS.w_sec_zi[offset] >= 0);
        REQUIRE(SDS.isotropy_zi[offset] >= 0);
        REQUIRE(SDS.thetal_zi[offset] >= 0);
      }
    }

    // Call the C++ implementation
    compute_diag_third_shoc_moment(SDS);

    // Check the result

    // Check to make sure there is at least one
    //  positive w3 value for convective boundary layer
    bool is_skew;
    // Verify that boundary points are zero
    for(Int s = 0; s < shcol; ++s) {
      is_skew = false; // Initialize
      for(Int n = 0; n < nlevi; ++n) {
        const auto offset = n + s * nlevi;
        //offset for "neighboring" column
        const auto offsets = n + (s+1) * nlevi;

        // Given this profile, values should fall within
        //  reasonable bounds
        REQUIRE(abs(SDS.w3[offset]) < 10);

        if (n == 0 || n == nlevi-1){
          REQUIRE(SDS.w3[n] == 0);
        }

        if (SDS.w3[offset] > 0){
          is_skew = true;
        }

        // Verify points increase in magnitude as
        //  scalar variances increase
        if (s < shcol-1 && n != 0 && n != nlevi-1){
          REQUIRE(std::abs(SDS.w3[offsets]) > std::abs(SDS.w3[offset]));
        }
      }
      // Verify each column has at least one positive vertical
      //   velocity skewness value
      REQUIRE(is_skew == true);
    }

    // SECOND TEST
    // If SHOC is reverted to a 1.5 TKE closure then test to make sure that
    //  all values of w3 are zero everywhere.  Will use the same input data
    //  as the previous test.

    // Activate 1.5 TKE closure assumptions
    SDS.shoc_1p5tke = true;

    // Call the C++ implementation
    compute_diag_third_shoc_moment(SDS);

    // Check the result

    // Require that all values of w3 are ZERO
    for (Int s = 0; s < shcol; ++s){
      for (Int n = 0; n < nlevi; ++n){
        const auto offset = n + s * nlevi;
        REQUIRE(SDS.w3[offset] == 0);
      }
    }

  }

  void run_bfb()
  {
    auto engine = Base::get_engine();

    ComputeDiagThirdShocMomentData SDS_baseline[] = {
      //               shcol, nlev, nlevi
      ComputeDiagThirdShocMomentData(10, 71, 72, false),
      ComputeDiagThirdShocMomentData(10, 12, 13, false),
      ComputeDiagThirdShocMomentData(7,  16, 17, false),
      ComputeDiagThirdShocMomentData(2, 7, 8, false)
    };

    // Generate random input data
    for (auto& d : SDS_baseline) {
      d.randomize(engine);
    }

    // Create copies of data for use by cxx. Needs to happen before reads so that
    // inout data is in original state
    ComputeDiagThirdShocMomentData SDS_cxx[] = {
      ComputeDiagThirdShocMomentData(SDS_baseline[0]),
      ComputeDiagThirdShocMomentData(SDS_baseline[1]),
      ComputeDiagThirdShocMomentData(SDS_baseline[2]),
      ComputeDiagThirdShocMomentData(SDS_baseline[3]),
    };

    static constexpr Int num_runs = sizeof(SDS_baseline) / sizeof(ComputeDiagThirdShocMomentData);

    // Assume all data is in C layout

    // Read baseline data
    if (this->m_baseline_action == COMPARE) {
      for (auto& d : SDS_baseline) {
        d.read(Base::m_ifile);
      }
    }

    // Get data from cxx
    for (auto& d : SDS_cxx) {
      compute_diag_third_shoc_moment(d);
    }

    // Verify BFB results, all data should be in C layout
    if (SCREAM_BFB_TESTING && this->m_baseline_action == COMPARE) {
      for (Int i = 0; i < num_runs; ++i) {
        ComputeDiagThirdShocMomentData& d_baseline = SDS_baseline[i];
        ComputeDiagThirdShocMomentData& d_cxx = SDS_cxx[i];
        for (Int k = 0; k < d_baseline.total(d_baseline.w3); ++k) {
          REQUIRE(d_baseline.w3[k] == d_cxx.w3[k]);
        }
      }
    } // SCREAM_BFB_TESTING
    else if (this->m_baseline_action == GENERATE) {
      for (Int i = 0; i < num_runs; ++i) {
        SDS_cxx[i].write(Base::m_ofile);
      }
    }
  }
};

}  // namespace unit_test
}  // namespace shoc
}  // namespace scream

namespace {

TEST_CASE("shoc_comp_diag_third_property", "shoc")
{
  using TestStruct = scream::shoc::unit_test::UnitWrap::UnitTest<scream::DefaultDevice>::TestShocCompDiagThird;

  TestStruct().run_property();
}

TEST_CASE("shoc_comp_diag_third_bfb", "shoc")
{
  using TestStruct = scream::shoc::unit_test::UnitWrap::UnitTest<scream::DefaultDevice>::TestShocCompDiagThird;

  TestStruct().run_bfb();
}

} // namespace
