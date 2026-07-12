/*
 * GT_RoadGen - standalone OpenDRIVE -> .osgb road-surface generator (GT_esmini)
 *
 * Generates a 3D road-surface model from an OpenDRIVE file using a per-road
 * MULTITHREADED tessellator (GT_RoadGeom). The output .osgb can be injected into
 * the esmini viewer via `--model`, which makes the core viewer SKIP its (slow,
 * single-threaded) runtime road-mesh generation. The generated geometry is
 * identical to upstream; only the generation is parallelized + cacheable.
 *
 * Coordinate frame: the model is wrapped in a double-precision MatrixTransform that
 * translates the near-origin float vertices back to absolute OpenDRIVE world
 * coordinates -- exactly the structure esmini's own `--save_generated_model` writes,
 * so a loaded model lines up with the (origin-shifted) dynamic entities and keeps
 * float precision on large maps.
 *
 * Usage: GT_RoadGen <input.xodr> <output.osgb> [--threads N]
 *        --threads 0 (default) = auto (hardware_concurrency); 1 = single-threaded.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 */

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>

#include <osg/Group>
#include <osg/MatrixTransform>
#include <osgDB/WriteFile>

#include "GT_RoadGeom.hpp"
#include "RoadManager.hpp"
#include "CommonMini.hpp"

static void PrintUsage()
{
    printf("GT_RoadGen - OpenDRIVE -> .osgb road-surface generator\n");
    printf("Usage: GT_RoadGen <input.xodr> <output.osgb> [--threads N]\n");
    printf("       --threads 0 (default) = auto, 1 = single-threaded\n");
}

int main(int argc, char* argv[])
{
    std::string xodr_file;
    std::string out_file;
    unsigned int n_threads = 0;  // 0 = auto

    // Positional args + optional --threads
    for (int i = 1; i < argc; i++)
    {
        std::string arg = argv[i];
        if (arg == "--threads" && i + 1 < argc)
        {
            n_threads = static_cast<unsigned int>(std::max(0, atoi(argv[++i])));
        }
        else if (arg == "--help" || arg == "-h")
        {
            PrintUsage();
            return 0;
        }
        else if (xodr_file.empty())
        {
            xodr_file = arg;
        }
        else if (out_file.empty())
        {
            out_file = arg;
        }
    }

    if (xodr_file.empty() || out_file.empty())
    {
        PrintUsage();
        return -1;
    }

    // Resolve OpenDRIVE-relative resource lookups (textures, models) the same way esmini does.
    SE_Env::Inst().GetOptions().SetOptionValue("path", DirNameOf(xodr_file), true, true);

    if (!roadmanager::Position::LoadOpenDrive(xodr_file.c_str()))
    {
        fprintf(stderr, "GT_RoadGen: failed to load OpenDRIVE file %s\n", xodr_file.c_str());
        return -1;
    }

    roadmanager::OpenDrive* odr = roadmanager::Position::GetOpenDrive();
    if (odr == nullptr || odr->GetNumOfRoads() == 0)
    {
        fprintf(stderr, "GT_RoadGen: no roads in %s\n", xodr_file.c_str());
        return -1;
    }

    // Establish the origin of the road network: coordinates of the first lane OSI point.
    // This MUST match the origin esmini's viewer computes (viewer.cpp), so the model lines
    // up with the origin-shifted entities when loaded via --model.
    osg::Vec3d origin(0.0, 0.0, 0.0);
    if (odr->GetRoadByIdx(0) && odr->GetRoadByIdx(0)->GetLaneSectionByIdx(0) &&
        odr->GetRoadByIdx(0)->GetLaneSectionByIdx(0)->GetLaneByIdx(0))
    {
        origin[0] = odr->GetRoadByIdx(0)->GetLaneSectionByIdx(0)->GetLaneByIdx(0)->GetOSIPoints()->GetXfromIdx(0);
        origin[1] = odr->GetRoadByIdx(0)->GetLaneSectionByIdx(0)->GetLaneByIdx(0)->GetOSIPoints()->GetYfromIdx(0);
    }

    roadgeom::RoadGeom::SetNumThreads(n_threads);

    __int64 t_start = SE_getSystemTimeMilliseconds();

    // surface + road marks, NO road objects (the core viewer still adds signs/objects at
    // load time); optimize=false to preserve structure for serialization.
    roadgeom::RoadGeom road_geom(odr,
                                 nullptr,
                                 origin,
                                 true,   // generate_road_surface
                                 false,  // generate_road_objects
                                 argv[0],
                                 false);  // optimize

    __int64 t_gen = SE_getSystemTimeMilliseconds();

    if (road_geom.root_ == nullptr)
    {
        fprintf(stderr, "GT_RoadGen: road geometry generation produced no model\n");
        return -1;
    }

    // Wrap in a double-precision translate so the near-origin float vertices render at
    // absolute world coordinates when loaded as an external --model (mirrors envGroup_).
    osg::ref_ptr<osg::MatrixTransform> origin_tx = new osg::MatrixTransform;
    origin_tx->setName("gt_road_origin2odr");
    origin_tx->setMatrix(osg::Matrix::translate(origin));
    origin_tx->addChild(road_geom.root_.get());

    if (!osgDB::writeNodeFile(*origin_tx, out_file))
    {
        fprintf(stderr, "GT_RoadGen: failed to write %s\n", out_file.c_str());
        return -1;
    }

    __int64 t_write = SE_getSystemTimeMilliseconds();

    printf("GT_RoadGen: %s -> %s (roads=%d, threads=%u, gen=%lld ms, write=%lld ms, origin=%.2f,%.2f)\n",
           FileNameOf(xodr_file).c_str(),
           out_file.c_str(),
           odr->GetNumOfRoads(),
           n_threads,
           static_cast<long long>(t_gen - t_start),
           static_cast<long long>(t_write - t_gen),
           origin[0],
           origin[1]);

    // The .osgb is fully written and flushed by writeNodeFile above. Skip C++ static destructors
    // (OSG static plugins / protobuf teardown can __fastfail at exit, returning a non-zero code
    // even though the artifact is complete) by exiting hard with success.
    fflush(stdout);
    fflush(stderr);
    std::_Exit(0);
}
