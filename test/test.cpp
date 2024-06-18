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
static constexpr double TwoPi = 6.28318530717958647692528676656;

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

    pgf::Axis p;
    try
    {
        const auto data = pgf::mesh_grid([](double x, double y){ return std::
            sin(x)*std::sin(y); }, 0., 1., 0., 1., 50);
        p.surf(data[0], data[1], data[2]);
        p.setXMin(0.);
        p.setXMax(1.);
        p.setYMin(0.);
        p.setYMax(1.);
        p.setZMin(0.);
        p.setZMax(1.);
        p.setView(45., 45.);
        p.setXLabel("$x$ (\\si{\\lu})");
        p.setYLabel("$y$ (\\si{\\arcsec})");
        p.setZLabel("$\\dv{^2x}{y^2}$ (\\si{\\lu^2\\per\\arcsec^2})");
        p.setTitle("Test Plot");
        p.resize(0.6, 0.6);
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

    pgf::Axis q;
    try
    {
        pgf::DrawStyle style = pgf::Default;
        style.markStyle.spacing = 20;
        std::vector<double> x(200);
        std::vector<std::vector<double>> y(5, std::vector<double>(x.size()));
        for(std::size_t i = 0; i < x.size(); ++i)
        {
            x[i] = i*TwoPi/(x.size() - 1);
            y[0][i] = std::cos(x[i]);
            y[1][i] = 1. + std::sin(x[i]);
            y[2][i] = std::max(0., std::min(1., std::tan(x[i])));
            y[3][i] = 1. - std::cos(x[i]);
            y[4][i] = std::cos(std::cos(x[i]));
        }
        q.draw(style, x, y[0], {}, {}, "$\\cos x$");
        q.draw(style, x, y[1], {}, {}, "$1+\\sin x$");
        q.draw(style, x, y[2], {}, {}, "$\\operatornamewithlimits{clamp}_{\\lef"
            "t[0,\\,1\\right]}\\tan x$");
        q.draw(style, x, y[3], {}, {}, "$1-\\cos x$");
        q.draw(style, x, y[4], {}, {}, "$\\cos\\args{\\cos x}$");
        q.bgBands({0., 1., 2., 3., 4., 5., 6., x.back()});
        q.squeezeX();
        q.setYMin(-1.1);
        q.setYMax(2.1);
        q.resize(1., 0.6);
        q.setTitle("Other Plot");
        q.setXLabel("$x$");
        q.setYLabel("$y$");
        q.legend(pgf::Axis::Southeast);
    }
    CATCH

    try
    {
        pgf::plot(outputDir + "/" + PlotName + "-1", p, q);
    }
    CATCH

    try
    {
        const std::vector<pgf::Axis> v = {p, q};
        pgf::plot(outputDir + "/" + PlotName + "-2", v);
    }
    CATCH
}
