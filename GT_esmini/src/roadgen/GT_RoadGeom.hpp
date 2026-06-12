/*
 * esmini - Environment Simulator Minimalistic
 * https://github.com/esmini/esmini
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * Copyright (c) partners of Simulation Scenarios
 * https://sites.google.com/view/simulationscenarios
 */

// =====================================================================================
// GT_esmini fork of EnvironmentSimulator/Modules/ViewerBase/roadgeom.hpp
// Fork source: esmini commit 8773c463.
//
// Purpose: a per-road MULTITHREADED road-surface generator. The geometry produced is
// byte-identical to the upstream (sequential) RoadGeom; only the surface tessellation is
// parallelized across roads. Built into the standalone GT_RoadGen tool, NOT linked into
// the core RoadGeom/ViewerBase libraries (so namespace `roadgeom` does not clash).
//
// Re-sync policy: when upstream ViewerBase/roadgeom.* changes, re-copy and re-apply the
// parallelization markers tagged "GT-PARALLEL".
// =====================================================================================
#ifndef GT_ROADGEOM_HPP_
#define GT_ROADGEOM_HPP_

#include <vector>
#include <mutex>
#include <osg/PositionAttitudeTransform>
#include <osg/Texture2D>
#include <osg/Group>
#include <osg/Geometry>
#include <osg/Material>
#include "RoadManager.hpp"
#include "GT_trafficlightmodel.hpp"

namespace roadgeom
{
    typedef enum
    {
        NODE_MASK_NONE             = (0),
        NODE_MASK_OBJECT_SENSORS   = (1 << 0),
        NODE_MASK_TRAIL_LINES      = (1 << 1),
        NODE_MASK_TRAIL_DOTS       = (1 << 2),
        NODE_MASK_ODR_FEATURES     = (1 << 3),
        NODE_MASK_OSI_POINTS       = (1 << 4),
        NODE_MASK_OSI_LINES        = (1 << 5),
        NODE_MASK_ENV_MODEL        = (1 << 6),
        NODE_MASK_ENTITY_MODEL     = (1 << 7),
        NODE_MASK_ENTITY_BB        = (1 << 8),
        NODE_MASK_INFO             = (1 << 9),
        NODE_MASK_INFO_PER_OBJ     = (1 << 10),
        NODE_MASK_ROAD_SENSORS     = (1 << 11),
        NODE_MASK_TRAJECTORY_LINES = (1 << 12),
        NODE_MASK_ROUTE_WAYPOINTS  = (1 << 13),
        NODE_MASK_SIGN             = (1 << 14),
        NODE_MASK_WEATHER          = (1 << 15),
        NODE_MASK_OBJ_OUTLINE      = (1 << 16),
        NODE_MASK_AXIS_INDICATOR   = (1 << 17),
        NODE_MASK_ENTITY_BB_FILLED = (1 << 18),
    } NodeMask;

    uint64_t  GenerateMaterialKey(double r, double g, double b, double a, uint8_t t, uint8_t f);
    osg::Vec4 ODR2OSGColor(roadmanager::RoadMarkColor color);

    class RoadGeom
    {
    public:
        enum class MaterialType : uint8_t
        {
            NONE = 0,
            ASPHALT,
            GRASS,
            ROADMARK,
            CONCRETE,
            BORDER
        };

        osg::ref_ptr<osg::Group>                                  root_;
        osg::ref_ptr<osg::Vec4Array>                              color_asphalt_ = new osg::Vec4Array;
        std::unordered_map<uint64_t, osg::ref_ptr<osg::Material>> std_materials_ = {};

        /**
            Create 3D model of the road network from OpenDRIVE data
            @param odr Pointer to OpenDRIVE object
            @param environment Pointer to environment model node, parent of the world model
            @param origin Origin offset to apply to all coordinates
            @param generate_road_surface If true, generate road surface geometry
            @param generate_road_objects If true, generate road objects (signs, barriers, etc.)
            @param exe_path Path to executable, used to find resources
            @param optimize Set to false if model will be saved to file, true if used for immediate visualization
        */
        RoadGeom(roadmanager::OpenDrive* odr,
                 osg::Node*              environment,
                 osg::Vec3d              origin,
                 bool                    generate_road_surface,
                 bool                    generate_road_objects,
                 std::string             exe_path,
                 bool                    optimize);

        int                         AddRoadMarks(roadmanager::Lane* lane, osg::Group* rm_group, const osg::Vec3d& origin);
        void                        AddRoadMarkGeom(osg::ref_ptr<osg::Vec3Array>        vertices,
                                                    osg::ref_ptr<osg::DrawElementsUInt> indices,
                                                    osg::Group*                         rm_group,
                                                    const roadmanager::LaneRoadMark&    road_mark,
                                                    double                              fade);
        osg::ref_ptr<osg::Material> GetOrCreateMaterial(const std::string& basename, osg::Vec4 color, uint8_t texture_type, uint8_t has_friction = 0);
        osg::ref_ptr<osg::Texture2D> ReadTexture(std::string filename, bool log_missing_file = true);
        osg::Vec4                    GetFrictionColor(const double friction);

        int                                          CreateRoadSignsAndObjects(roadmanager::OpenDrive*  od,
                                                                               const osg::Vec3d&        origin,
                                                                               bool                     stand_in_model,
                                                                               osg::ref_ptr<osg::Group> parent,
                                                                               std::string              exe_path);
        osg::ref_ptr<osg::PositionAttitudeTransform> LoadRoadFeature(roadmanager::Road* road, std::string file_path);
        osg::ref_ptr<osg::Group>                     CreateOutlineObject(roadmanager::Outline* outline, osg::Vec4 color, const osg::Vec3d& origin);
        int                                          AddGroundSurface();
        void                                         SetNodeName(osg::Node& node, const std::string& prefix, id_t id, const std::string& label);
        int                                          SaveToFile(const std::string& filename);
        TrafficLightModel*                           GetTrafficLightModel(int id);

        // GT-PARALLEL: worker-thread count for road-surface generation.
        // 0 = auto (hardware_concurrency); 1 = single-threaded (identical to upstream path).
        // Static so callers can set the count before constructing a RoadGeom.
        static void                                  SetNumThreads(unsigned int n);
        static unsigned int                          GetNumThreads();

        std::unordered_map<int, TrafficLightModel> traffic_light_;

    private:
        unsigned int                                                   number_of_materials     = 0;
        std::unordered_map<MaterialType, osg::ref_ptr<osg::Texture2D>> texture_map_            = {};
        double                                                         lane_friction_          = 1.0;
        std::mutex                                                     material_mutex_;  // GT-PARALLEL: guards std_materials_ / number_of_materials
        roadmanager::OpenDrive*                                        odrManager_             = nullptr;
        bool                                                           optimize_               = true;
        osg::Node*                                                     environment_            = nullptr;
        std::string                                                    exe_dir_                = "";
        int                                                            roadmark_texture_found_ = -1;
    };

}  // namespace roadgeom

#endif  // GT_ROADGEOM_HPP_
