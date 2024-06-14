#include "pgfplotter"

std::array<std::vector<double>, 3> pgfplotter::mesh_grid(const std::function<
    double(double, double)>& f, double xMin, double xMax, double yMin, double
    yMax, std::size_t res)
{
    return mesh_grid(f, xMin, xMax, yMin, yMax, res, res);
}

std::array<std::vector<double>, 3> pgfplotter::mesh_grid(const std::function<
    double(double, double)>& f, double xMin, double xMax, double yMin, double
    yMax, std::size_t xRes, std::size_t yRes)
{
    if(!yRes)
    {
        yRes = xRes;
    }

    std::array<std::vector<double>, 3> grid;
    for(auto& n : grid)
    {
        n.resize(xRes*yRes);
    }

    std::vector<double> x(xRes);
    std::vector<double> y(yRes);
    for(std::size_t i = 0; i < xRes; ++i)
    {
        x[i] = xMin + i*(xMax - xMin)/(xRes - 1);
    }
    for(std::size_t i = 0; i < yRes; ++i)
    {
        y[i] = yMin + i*(yMax - yMin)/(yRes - 1);
    }
    for(std::size_t i = 0; i < xRes; ++i)
    {
        for(std::size_t j = 0; j < yRes; ++j)
        {
            grid[0][yRes*i + j] = x[i];
            grid[1][yRes*i + j] = y[j];
            grid[2][yRes*i + j] = f(x[i], y[j]);
        }
    }

    return grid;
}
