//HighlightROIs.cc - A part of DICOMautomaton 2017, 2021. Written by hal clark.

#include <any>
#include <optional>
#include <functional>
#include <iterator>
#include <list>
#include <map>
#include <memory>
#include <regex>
#include <stdexcept>
#include <string>    

#include "../Structs.h"
#include "../Regex_Selectors.h"
#include "../YgorImages_Functors/Grouping/Misc_Functors.h"
#include "../YgorImages_Functors/Processing/Partitioned_Image_Voxel_Visitor_Mutator.h"

#include "HighlightROIs.h"

#include "YgorImages.h"
#include "YgorMath.h"         //Needed for vec3 class.
#include "YgorString.h"       //Needed for GetFirstRegex(...)
#include "YgorStats.h"



OperationDoc OpArgDocHighlightROIs(){
    OperationDoc out;
    out.name = "HighlightROIs";
    out.aliases.emplace_back("ConvertContoursToImages");

    out.desc = 
        "This operation overwrites voxel data inside and/or outside of ROI(s) to create an image"
        " representation of a set of contours."
        " It can handle overlapping or duplicate contours.";

    out.notes.emplace_back(
        "The 'receding_squares' implementation was developed with the expectations that:"
        " (1) the entire image will be overwritten, (2) contours are accurate and selective, so that"
        " ContourOverlap should be either 'honour_opposite_orientations' or 'overlapping_contours_cancel',"
        " and that (3) the contour detail and image grid resolution are sufficiently matched that it is"
        " uncommon for multiple contours to pass between adjacent voxels."
        " Expectation (3) could significantly impact round-trip contour accuracy, so consider using"
        " high-resolution images and, if possible, avoid pathological contours (e.g., multiple colinear"
        " contours separated by small distances)."
    );
    out.notes.emplace_back(
        "The 'receding_squares' method works best when all values (interior and exterior) can be"
        " overwritten. This affords the most control and gives the most accurate results."
        " If some values cannot be overwritten, the algorithm will try to account for the loss of"
        " freedom, but may be too constrained. If this is necessary, consider providing a large"
        " voxel value range."
    );
    out.notes.emplace_back(
        "Inclusivity option does not apply to the 'receding_squares' method."
    );
    out.notes.emplace_back(
        "Neither 'receding_squares' nor 'binary' methods require InteriorVal and ExteriorVal to be ordered."
    );

    out.args.emplace_back();
    out.args.back().name = "Channel";
    out.args.back().desc = "The image channel to use. Zero-based. Use '-1' to operate on all available channels.";
    out.args.back().default_val = "-1";
    out.args.back().expected = true;
    out.args.back().examples = { "-1", "0", "1", "2" };

    out.args.emplace_back();
    out.args.back() = IAWhitelistOpArgDoc();
    out.args.back().name = "ImageSelection";
    out.args.back().default_val = "last";

    out.args.emplace_back();
    out.args.back().name = "ContourOverlap";
    out.args.back().desc = "Controls overlapping contours are treated."
                           " The default 'ignore' treats overlapping contours as a single contour, regardless of"
                           " contour orientation. The option 'honour_opposite_orientations' makes overlapping contours"
                           " with opposite orientation cancel. Otherwise, orientation is ignored. The latter is useful"
                           " for Boolean structures where contour orientation is significant for interior contours (holes)."
                           " The option 'overlapping_contours_cancel' ignores orientation and cancels all contour overlap.";
    out.args.back().default_val = "ignore";
    out.args.back().expected = true;
    out.args.back().examples = { "ignore", "honour_opposite_orientations", 
                            "overlapping_contours_cancel", "honour_opps", "overlap_cancel" }; 
    out.args.back().samples = OpArgSamples::Exhaustive;

    out.args.emplace_back();
    out.args.back().name = "Inclusivity";
    out.args.back().desc = "Controls how voxels are deemed to be 'within' the interior of the selected ROI(s)."
                           " The default 'center' considers only the central-most point of each voxel."
                           " There are two corner options that correspond to a 2D projection of the voxel onto the image plane."
                           " The first, 'planar_corner_inclusive', considers a voxel interior if ANY corner is interior."
                           " The second, 'planar_corner_exclusive', considers a voxel interior if ALL (four) corners are interior.";
    out.args.back().default_val = "center";
    out.args.back().expected = true;
    out.args.back().examples = { "center", "centre", 
                                 "planar_corner_inclusive", "planar_inc",
                                 "planar_corner_exclusive", "planar_exc" };
    out.args.back().samples = OpArgSamples::Exhaustive;

    out.args.emplace_back();
    out.args.back().name = "Method";
    out.args.back().desc = "Controls the type of image mask that is generated. The default, 'binary', exclusively overwrites"
                           " voxels with the InteriorValue or ExteriorValue. Another method is 'receding_squares'"
                           " which creates a mask which, if processed with the marching-squares algorithm, will (mostly) recreate"
                           " the original contours. The 'receding_squares' can be considered the inverse of the"
                           " marching-squares algorithm. Note that the 'receding_squares' implementation is not optimized for speed.";
    out.args.back().default_val = "binary";
    out.args.back().expected = true;
    out.args.back().examples = { "binary", "receding_squares" };
    out.args.back().samples = OpArgSamples::Exhaustive;

    out.args.emplace_back();
    out.args.back().name = "ExteriorVal";
    out.args.back().desc = "The value to give to voxels outside the specified ROI(s). For the 'binary'"
                           " method, note that this value will be ignored if exterior overwrites are disabled."
                           " For the 'receding_squares' method this value is used to define the threshold"
                           " needed to recover the original contours (mean of InteriorVal and ExteriorVal).";
    out.args.back().default_val = "0.0";
    out.args.back().expected = true;
    out.args.back().examples = { "0.0", "-1.0", "1.23", "2.34E26" };

    out.args.emplace_back();
    out.args.back().name = "InteriorVal";
    out.args.back().desc = "The value to give to voxels within the specified ROI(s). For the 'binary'"
                           " method, note that this value will be ignored if interior overwrites are disabled."
                           " For the 'receding_squares' method this value is used to define the threshold"
                           " needed to recover the original contours (mean of InteriorVal and ExteriorVal).";
    out.args.back().default_val = "1.0";
    out.args.back().expected = true;
    out.args.back().examples = { "0.0", "-1.0", "1.23", "2.34E26" };

    out.args.emplace_back();
    out.args.back().name = "ExteriorOverwrite";
    out.args.back().desc = "Whether to overwrite voxels exterior to the specified ROI(s).";
    out.args.back().default_val = "true";
    out.args.back().expected = true;
    out.args.back().examples = { "true", "false" };

    out.args.emplace_back();
    out.args.back().name = "InteriorOverwrite";
    out.args.back().desc = "Whether to overwrite voxels interior to the specified ROI(s).";
    out.args.back().default_val = "true";
    out.args.back().expected = true;
    out.args.back().examples = { "true", "false" };

    out.args.emplace_back();
    out.args.back() = NCWhitelistOpArgDoc();
    out.args.back().name = "NormalizedROILabelRegex";
    out.args.back().default_val = ".*";

    out.args.emplace_back();
    out.args.back() = RCWhitelistOpArgDoc();
    out.args.back().name = "ROILabelRegex";
    out.args.back().default_val = ".*";

    return out;
}



Drover HighlightROIs(Drover DICOM_data,
                     const OperationArgPkg& OptArgs,
                     const std::map<std::string, std::string>& /*InvocationMetadata*/,
                     const std::string& /*FilenameLex*/){

    //---------------------------------------------- User Parameters --------------------------------------------------
    const auto Channel = std::stol( OptArgs.getValueStr("Channel").value() );
    const auto ImageSelectionStr = OptArgs.getValueStr("ImageSelection").value();
    const auto InclusivityStr = OptArgs.getValueStr("Inclusivity").value();
    const auto ContourOverlapStr = OptArgs.getValueStr("ContourOverlap").value();
    const auto MethodStr = OptArgs.getValueStr("Method").value();

    const auto ExteriorVal = std::stod(OptArgs.getValueStr("ExteriorVal").value());
    const auto InteriorVal = std::stod(OptArgs.getValueStr("InteriorVal").value());
    const auto ExteriorOverwriteStr = OptArgs.getValueStr("ExteriorOverwrite").value();
    const auto InteriorOverwriteStr = OptArgs.getValueStr("InteriorOverwrite").value();

    const auto NormalizedROILabelRegex = OptArgs.getValueStr("NormalizedROILabelRegex").value();
    const auto ROILabelRegex = OptArgs.getValueStr("ROILabelRegex").value();

    const bool ClampResult = false; // Only for receding_squares. Confines voxels within InteriorValue and ExteriorValue (inclusive).
    const auto RecedingThreshold = ExteriorVal * 0.5 + InteriorVal * 0.5;


    //-----------------------------------------------------------------------------------------------------------------

    const auto TrueRegex = Compile_Regex("^tr?u?e?$");

    const auto regex_centre = Compile_Regex("^ce?n?t?[re]?[er]?");
    const auto regex_pci = Compile_Regex("^pl?a?n?a?r?[_-]?c?o?r?n?e?r?s?[_-]?inc?l?u?s?i?v?e?$");
    const auto regex_pce = Compile_Regex("^pl?a?n?a?r?[_-]?c?o?r?n?e?r?s?[_-]?exc?l?u?s?i?v?e?$");

    const auto regex_ignore = Compile_Regex("^ig?n?o?r?e?$");
    const auto regex_honopps = Compile_Regex("^ho?n?o?u?r?_?o?p?p?o?s?i?t?e?[_-]?o?r?i?e?n?t?a?t?i?o?n?s?$");
    const auto regex_cancel = Compile_Regex("^ov?e?r?l?a?p?p?i?n?g?[_-]?c?o?n?t?o?u?r?s?[_-]?c?a?n?c?e?l?s?$");

    const auto regex_binary = Compile_Regex("^bi?n?a?r?y?$");
    const auto regex_recede = Compile_Regex("^re?c?e?d?i?n?g?[_-]?s?q?u?a?r?e?s?$");

    const auto ShouldOverwriteExterior = std::regex_match(ExteriorOverwriteStr, TrueRegex);
    const auto ShouldOverwriteInterior = std::regex_match(InteriorOverwriteStr, TrueRegex);

    //Stuff references to all contours into a list. Remember that you can still address specific contours through
    // the original holding containers (which are not modified here).
    auto cc_all = All_CCs( DICOM_data );
    auto cc_ROIs = Whitelist( cc_all, { { "ROIName", ROILabelRegex },
                                        { "NormalizedROIName", NormalizedROILabelRegex } } );
    if(cc_ROIs.empty()){
        throw std::invalid_argument("No contours selected. Cannot continue.");
    }

const bool contour_overlap_ignore  = std::regex_match(ContourOverlapStr, regex_ignore);
const bool contour_overlap_honopps = std::regex_match(ContourOverlapStr, regex_honopps);
const bool contour_overlap_cancel  = std::regex_match(ContourOverlapStr, regex_cancel);
    const auto f_receding_squares = [&](bool is_interior,
                                        long int r,
                                        long int c,
                                        long int channel,
                                        std::reference_wrapper<planar_image<float,double>> img_refw,
                                        std::reference_wrapper<planar_image<float,double>> mask_img_refw,
                                        float &val ) -> void {

        const auto nan = std::numeric_limits<double>::quiet_NaN();
        const auto eps = 10.0 * std::sqrt( std::numeric_limits<double>::min() );
        const double just_exterior = (RecedingThreshold * 0.999 + ExteriorVal * 0.001);
        const double just_interior = (RecedingThreshold * 0.999 + InteriorVal * 0.001);

        const auto mask_chnl = mask_img_refw.get().channels - 1;
        const auto M_r0c0 = mask_img_refw.get().value(r,c,mask_chnl);
        const auto M_rmc0 = (isininc(0,r-1,img_refw.get().rows-1)) ? mask_img_refw.get().value(r-1,c,mask_chnl) : 0;
        const auto M_r0cm = (isininc(0,c-1,img_refw.get().columns-1)) ? mask_img_refw.get().value(r,c-1,mask_chnl) : 0;

        // Short-circuit the calculation if there are no large contours that delineate this voxel from its neighbours.
        if( (M_r0c0 == M_rmc0) 
        &&  (M_r0c0 == M_r0cm) ){
            val = (is_interior) ? just_interior : just_exterior;
            return;
        }

        // This algorithm uses the left (c-1) and top (r-1) neighbours and any contours that pass between them and this
        // voxel (r,c) to determine an appropriate value for this voxel.
        const auto pos_r0c0 = img_refw.get().position(r, c);
        const auto pos_rmc0 = pos_r0c0 - img_refw.get().row_unit * img_refw.get().pxl_dx;
        const auto pos_r0cm = pos_r0c0 - img_refw.get().col_unit * img_refw.get().pxl_dy;

        const auto I_r0c0 = val;
        const auto I_rmc0 = (isininc(0,r-1,img_refw.get().rows-1)) ? img_refw.get().value(r-1, c, channel) : just_exterior;
        const auto I_r0cm = (isininc(0,c-1,img_refw.get().columns-1)) ? img_refw.get().value(r, c-1, channel) : just_exterior;


        const line<double> l_l(pos_rmc0, pos_r0c0);
        const line<double> l_t(pos_r0cm, pos_r0c0);
        const auto dist_l = img_refw.get().pxl_dx;
        const auto dist_t = img_refw.get().pxl_dy;
        const auto img_plane = img_refw.get().image_plane();

        double newval = I_r0c0;
        std::vector<double> newvals; // Estimates of what the modified intensity should be.
        long int within_pos_count = 0;
        long int within_neg_count = 0;

        const auto do_loop = [&](){
            for(auto &cc_refw : cc_ROIs){
                for(auto &c : cc_refw.get().contours){

                    // NOTE: The current implementation of Mutate_Voxels() differs from the following explicit
                    // is_point_in_poly() check. Using this version seems to be necessary for accurate contours in this
                    // case. Need to investigate further. TODO.
                    if( contour_overlap_ignore ){
                        const bool already_proj = true;
                        const bool is_within = c.Is_Point_In_Polygon_Projected_Orthogonally(img_plane, pos_r0c0, already_proj);
                        if(is_within){
                            const auto c_N = c.Estimate_Planar_Normal();
                            if(0.0 <= c_N.Dot(img_plane.N_0)){
                                ++within_pos_count;
                            }else{
                                ++within_neg_count;
                            }
                        }
                    }

                    auto itB = std::prev( std::end(c.points) );
                    for(auto itA = std::begin(c.points); itA != std::end(c.points); ++itA){
                        const double dist_v = itA->distance(*itB);
                        if(eps < dist_v){
                            const line<double> l_v(*itA, *itB);

                            // Determine if the line segments intersect. (Is there a categorically faster way?)
                            vec3<double> p_t;
                                                        // Pre-filtering starts here.
                            const bool intersect_t =    (pos_r0c0.distance(*itA) <= (dist_v + dist_t))
                                                     && (pos_r0c0.distance(*itB) <= (dist_v + dist_t))
                                                        // Line segment intersection tests start here.
                                                     && l_t.Intersects_With_Line_Once(l_v, p_t)
                                                     && (p_t.distance(*itA) <= dist_v)
                                                     && (p_t.distance(*itB) <= dist_v)
                                                     && (p_t.distance(pos_r0cm) <= dist_t)
                                                     && (p_t.distance(pos_r0c0) <= dist_t);
                            if(intersect_t){
                                // Assume linear interpolation, which marching-squares would typically use.
                                // Invert the 'slope' of a linear interpolation based on threshold extraction.
                                const auto inv_m_t = dist_t / (dist_t - p_t.distance(pos_r0c0));
                                if(std::isfinite(inv_m_t)){
                                    newvals.push_back( I_r0cm - (I_r0cm - RecedingThreshold) * inv_m_t);
                                }
                            }

                            vec3<double> p_l;
                                                        // Pre-filtering starts here.
                            const bool intersect_l =    (pos_r0c0.distance(*itA) <= (dist_v + dist_l))
                                                     && (pos_r0c0.distance(*itB) <= (dist_v + dist_l))
                                                        // Line segment intersection tests start here.
                                                     && l_l.Intersects_With_Line_Once(l_v, p_l)
                                                     && (p_l.distance(*itA) <= dist_v)
                                                     && (p_l.distance(*itB) <= dist_v)
                                                     && (p_l.distance(pos_rmc0) <= dist_l)
                                                     && (p_l.distance(pos_r0c0) <= dist_l);
                            if(intersect_l){
                                // Assume linear interpolation, which marching-squares would typically use.
                                // Invert the 'slope' of a linear interpolation based on threshold extraction.
                                const auto inv_m_l = dist_l / (dist_l - p_l.distance(pos_r0c0));
                                if(std::isfinite(inv_m_l)){
                                    newvals.push_back( I_rmc0 - (I_rmc0 - RecedingThreshold) * inv_m_l);
                                }
                            }
                        }
                        itB = itA;
                    }
                }
            }
        };
        do_loop();

        {
            if(false){
            }else if( contour_overlap_ignore ){
                const auto within_count = (within_pos_count + within_neg_count);
                is_interior = (within_count != 0);
                // Ignore contours after the first.
                if( is_interior ){
                    newvals.clear();
                }

            }else if( contour_overlap_honopps ){
                if( (std::abs(M_r0c0 + M_rmc0) == 1) 
                ||  (std::abs(M_r0c0 + M_r0cm) == 1) ){
                    // Do nothing.
                }else{
                    newvals.clear();
                }
                
            }else if( contour_overlap_cancel ){
                // Do nothing.

            }else{
                throw std::invalid_argument("ContourOverlap argument '"_s + ContourOverlapStr + "' is not valid");
            }

            if(false){
            }else if( is_interior ){
                newval = (newvals.empty()) ? just_interior : Stats::Mean(newvals);
            }else{
                newval = (newvals.empty()) ? just_exterior : Stats::Mean(newvals);
            }
        }

        // Confine the voxel value to the interior and exterior values, to bound the output.
        // Note: this step is NOT needed, and can result in lower accuracy.
        if(ClampResult){
            newval = std::clamp(newval, std::min(ExteriorVal,InteriorVal), std::max(ExteriorVal,InteriorVal));
        }
        val = newval;
        return;
    };
    const auto f_receding_squares_interior = std::bind(f_receding_squares, true,
                                                       std::placeholders::_1,
                                                       std::placeholders::_2,
                                                       std::placeholders::_3,
                                                       std::placeholders::_4,
                                                       std::placeholders::_5,
                                                       std::placeholders::_6);
    const auto f_receding_squares_exterior = std::bind(f_receding_squares, false,
                                                       std::placeholders::_1,
                                                       std::placeholders::_2,
                                                       std::placeholders::_3,
                                                       std::placeholders::_4,
                                                       std::placeholders::_5,
                                                       std::placeholders::_6);
 

    auto IAs_all = All_IAs( DICOM_data );
    auto IAs = Whitelist( IAs_all, ImageSelectionStr );
    for(auto & iap_it : IAs){
        PartitionedImageVoxelVisitorMutatorUserData ud;

        ud.mutation_opts.editstyle = Mutate_Voxels_Opts::EditStyle::InPlace;
        ud.mutation_opts.aggregate = Mutate_Voxels_Opts::Aggregate::First;
        ud.mutation_opts.adjacency = Mutate_Voxels_Opts::Adjacency::SingleVoxel;
        ud.mutation_opts.maskmod   = Mutate_Voxels_Opts::MaskMod::Noop;
        ud.description = "Highlighted ROIs";

        if( std::regex_match(ContourOverlapStr, regex_ignore) ){
            ud.mutation_opts.contouroverlap = Mutate_Voxels_Opts::ContourOverlap::Ignore;
        }else if( std::regex_match(ContourOverlapStr, regex_honopps) ){
            ud.mutation_opts.contouroverlap = Mutate_Voxels_Opts::ContourOverlap::HonourOppositeOrientations;
        }else if( std::regex_match(ContourOverlapStr, regex_cancel) ){
            ud.mutation_opts.contouroverlap = Mutate_Voxels_Opts::ContourOverlap::ImplicitOrientations;
        }else{
            throw std::invalid_argument("ContourOverlap argument '"_s + ContourOverlapStr + "' is not valid");
        }
        if( std::regex_match(InclusivityStr, regex_centre) ){
            ud.mutation_opts.inclusivity = Mutate_Voxels_Opts::Inclusivity::Centre;
        }else if( std::regex_match(InclusivityStr, regex_pci) ){
            ud.mutation_opts.inclusivity = Mutate_Voxels_Opts::Inclusivity::Inclusive;
        }else if( std::regex_match(InclusivityStr, regex_pce) ){
            ud.mutation_opts.inclusivity = Mutate_Voxels_Opts::Inclusivity::Exclusive;
        }else{
            throw std::invalid_argument("Inclusivity argument '"_s + InclusivityStr + "' is not valid");
        }

        Mutate_Voxels_Functor<float,double> f_noop;
        ud.f_bounded = f_noop;
        ud.f_unbounded = f_noop;
        ud.f_visitor = f_noop;

        if(std::regex_match(MethodStr, regex_binary)){
            if(ShouldOverwriteInterior){
                ud.f_bounded = [&](long int /*row*/, long int /*col*/, long int chan,
                                   std::reference_wrapper<planar_image<float,double>> /*img_refw*/,
                                   std::reference_wrapper<planar_image<float,double>> /*mask_img_refw*/,
                                   float &voxel_val) {
                    if( (Channel < 0) || (Channel == chan) ){
                        voxel_val = InteriorVal;
                    }
                };
            }
            if(ShouldOverwriteExterior){
                ud.f_unbounded = [&](long int /*row*/, long int /*col*/, long int chan,
                                     std::reference_wrapper<planar_image<float,double>> /*img_refw*/,
                                     std::reference_wrapper<planar_image<float,double>> /*mask_img_refw*/,
                                     float &voxel_val) {
                    if( (Channel < 0) || (Channel == chan) ){
                        voxel_val = ExteriorVal;
                    }
                };
            }

        }else if(std::regex_match(MethodStr, regex_recede)){
            //ud.f_visitor = f_receding_squares;

            if(ShouldOverwriteInterior){
                ud.f_bounded = f_receding_squares_interior;
            }
            if(ShouldOverwriteExterior){
                ud.f_unbounded = f_receding_squares_exterior;
            }

        }else{
            throw std::invalid_argument("Method argument '"_s + MethodStr + "' is not valid");
        }

        if(!(*iap_it)->imagecoll.Process_Images_Parallel( GroupIndividualImages,
                                                          PartitionedImageVoxelVisitorMutator,
                                                          {}, cc_ROIs, &ud )){
            throw std::runtime_error("Unable to highlight voxels within the specified ROI(s).");
        }
    }

    return DICOM_data;
}
