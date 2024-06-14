#include "pgfplotter"
#include <filesystem>
#include <cmath>
#include <sstream>
#include <fstream>

#define CATCH \
    catch(const std::exception& e) \
    { \
        std::cerr << "Failed " << test << ": " << e.what() << std::endl; \
        return 1; \
    } \
    std::cout << "Passed " << test << std::endl; \
    ++test;

namespace pgf = pgfplotter;

static const std::string PlotName = "plot";

static std::string get_dir(const std::string& path)
{
    if(path.empty())
    {
        return {};
    }
    const std::filesystem::path p(path);
    return p.parent_path().string();
}

int main(int, char** argv)
{
    int test = 1;

    std::string outputDir;
    try
    {
        outputDir = get_dir(argv[0]) + "/output";
        std::filesystem::remove_all(outputDir);
        std::filesystem::create_directory(outputDir);
    }
    CATCH

    pgf::Plotter p;
    try
    {
        const auto data = pgfplotter::mesh_grid([](double x, double y){ return
            std::sin(x)*std::sin(y); }, 0., 1., 0., 1., 50);
        p.surf(data[0], data[1], data[2]);
        p.x_min(0.);
        p.x_max(1.);
        p.y_min(0.);
        p.y_max(1.);
        p.z_min(0.);
        p.z_max(1.);
        p.view(45., 45.);
        p.setXLabel("$x$ (\\si{\\lu})");
        p.setYLabel("$y$ (\\si{\\arcsec})");
        p.setZLabel("$\\dv{^2x}{y^2}$ (\\si{\\lu^2\\per\\arcsec^2})");
        p.setTitle("Test Plot");
        pgf::plot(outputDir + "/" + PlotName, p);
    }
    CATCH

    try
    {
        if(!std::filesystem::exists(outputDir + "/" + PlotName + ".png"))
        {
            std::ostringstream oss;
            oss << "Did not generate plot";
            std::ifstream in(outputDir + "/" + PlotName + "_plot_data/" +
                PlotName + ".log");
            if(in)
            {
                oss << ":" << std::endl;
                std::string line;
                while(std::getline(in, line))
                {
                    oss << line << std::endl;
                }
            }
            else
            {
                oss << ". No log generated.";
            }
            throw std::runtime_error(oss.str());
        }
    }
    CATCH

    try
    {
        if(!std::filesystem::exists(outputDir + "/" + PlotName +
            "_plot_data.zip"))
        {
            throw std::runtime_error("Did zip data directory.");
        }
    }
    CATCH

    try
    {
        if(std::filesystem::exists(outputDir + "/" + PlotName + "_plot_data"))
        {
            throw std::runtime_error("Did not delete data directory.");
        }
    }
    CATCH

    try
    {
        pgf::plot(outputDir + "/" + PlotName + "-1", p, p);
    }
    CATCH

    try
    {
        std::vector<pgf::Plotter> v(2, p);
        pgf::plot(outputDir + "/" + PlotName + "-2", v);
    }
    CATCH
}
