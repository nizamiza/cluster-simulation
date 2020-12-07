#pragma once
#include "cluster.h"
#include "window.h"

namespace ntf::cluster
{
    template <typename T = int32_t>
    struct k_means : public partitioner<T>
    {
        std::default_random_engine random_engine;

        k_means()
        {
            this->name = "K means";
            this->param_name = "K";

            window::seed_default_random_engine(this->random_engine);
        }

        static v2d<T> compute_centroid(const cluster<T>& cluster)
        {
            v2d<T> coords_sum{};

            for (auto& observation : cluster.observations)
                coords_sum += *observation;

            return coords_sum / std::max(static_cast<T>(cluster.observations.size()), 1);
        };

        static bool converged(const std::vector<cluster<T>>& clusters, const std::vector<v2d<T>>& previous_means)
        {
            for (size_t i = 0; i < clusters.size(); i++)
            {
                if (clusters[i].mean != previous_means[i])
                    return false;
            }

            return true;
        };

        static std::vector<v2d<T>> find_optimal_means(size_t k, std::vector<v2d<T>>& observations)
        {
            std::vector<v2d<T>> means(k);

            v2d<T> plane_start{};
            v2d<T> plane_end{};

            for (auto& observation : observations)
            {
                if (observation.x < plane_start.x) plane_start.x = observation.x;
                if (observation.y < plane_start.y) plane_start.y = observation.y;
                if (observation.x > plane_end.x)   plane_end.x   = observation.x;
                if (observation.y > plane_end.y)   plane_end.y   = observation.y;
            }

            v2d<T> plane_centroid = (plane_start + plane_end) / 2;

            std::sort(
                observations.begin(),
                observations.end(),
                [](const v2d<T>& a, const v2d<T>& b) { return a.x < b.x; }
            );

            int32_t plane_sections_width = (plane_end.x - plane_start.x) / static_cast<int32_t>(k);

            for (size_t i = 0; i < k; i++)
            {
                v2d<T> target_centroid{
                    plane_sections_width * static_cast<int32_t>(i) + plane_sections_width / 2,
                    plane_centroid.y
                };

                auto mean_iter = std::lower_bound(
                    observations.begin(),
                    observations.end(),
                    target_centroid,
                    [](const v2d<T>& obs, const v2d<T>& target) { return obs.x < target.x; }
                );

                v2d<T> mean{};

                if (mean_iter == observations.end())
                    mean = observations.back();
                else
                    mean = *mean_iter;

                means[i] = mean;
            }

            return means;
        }

        static std::vector<cluster<T>> init_clusters(size_t k, const std::vector<v2d<T>>& means)
        {
            std::vector<cluster<T>> clusters(k);

            for (size_t i = 0; i < k; i++)
            {
                auto& cluster = clusters[i];

                cluster.mean = means[i];
                cluster.color = VISUALLY_DISTINCT_COLORS[i];
            }

            return clusters;
        }

        static void assign_observations(std::vector<cluster<T>>& clusters, const std::vector<v2d<T>>& observations)
        {
            for (size_t i = 0; i < observations.size(); i++)
            {
                auto& observation = observations[i];

                double closest_distance = DBL_MAX;
                size_t closest_cluster_index = 0;

                for (size_t j = 0; j < clusters.size(); j++)
                {
                    auto& cluster = clusters[j];
                    double distance = observation.euclidean_distance_squared(cluster.mean);

                    if (distance < closest_distance)
                    {
                        closest_distance = distance;
                        closest_cluster_index = j;
                    }
                }

                clusters[closest_cluster_index].observations.push_back(
                    std::make_shared<v2d<T>>(observation)
                );
            }
        }

        std::vector<cluster<T>> partition(std::vector<v2d<T>>& observations, partitioning_profile& profile = {}) override
        {
            profile.reset();
            timer t(profile.elapsed_time);

            std::uniform_int_distribution<size_t> indices_distribution(0, observations.size() - 1);
            std::unordered_map<size_t, bool> visited_indices;

            std::vector<cluster<T>> clusters(this->param);
            std::vector<v2d<T>> previous_means(this->param);

            for (size_t i = 0; i < this->param;)
            {
                size_t random_index = indices_distribution(this->random_engine);

                if (visited_indices.find(random_index) != visited_indices.end())
                    continue;

                clusters[i].mean = observations[random_index];
                clusters[i].color = VISUALLY_DISTINCT_COLORS[i];
                
                visited_indices.insert({ random_index, true });
                i++;
            }

            while (!k_means::converged(clusters, previous_means))
            {
                clear_clusters(clusters);

                k_means::assign_observations(clusters, observations);

                if (find_empty_cluster(clusters) != clusters.end())
                    return partition(observations, profile);

                for (size_t i = 0; i < clusters.size(); i++)
                {
                    auto& cluster = clusters[i];

                    previous_means[i] = cluster.mean;
                    cluster.mean = k_means::compute_centroid(clusters[i]);
                }

                profile.iterations++;
            }

            return clusters;
        }
    };

    template <typename T = int32_t>
    struct k_medoids : public partitioner<T>
    {
        std::default_random_engine random_engine;

        k_medoids()
        {
            this->name = "K medoids";
            this->param_name = "K";

            window::seed_default_random_engine(this->random_engine);
        }

        v2d<T> compute_medoid(const cluster<T>& cluster)
        {
            auto centroid = k_means<T>::compute_centroid(cluster);
            
            auto medoid_iter = std::lower_bound(
                cluster.observations.begin(),
                cluster.observations.end(),
                centroid,
                [](const v2d_shared_ptr<T> obs, const v2d<T>& centroid) { return obs->x < centroid.x; }
            );

            v2d<T> medoid{};

            if (medoid_iter == cluster.observations.end())
            {
                std::uniform_int_distribution<size_t> indices_distribution(0, cluster.observations.size() - 1);
                size_t index = indices_distribution(this->random_engine);
                
                medoid = *(cluster.observations.at(index));
            }

            else medoid = **medoid_iter;

            return medoid;
        };

        std::vector<cluster<T>> partition(std::vector<v2d<T>>& observations, partitioning_profile& profile = {}) override
        {
            profile.reset();
            timer t(profile.elapsed_time);

            std::vector<v2d<T>> optimal_means = std::move(k_means<T>::find_optimal_means(this->param, observations));
            std::vector<v2d<T>> previous_means(this->param);

            std::vector<cluster<T>> clusters = std::move(k_means<T>::init_clusters(this->param, optimal_means));

            double current_dissimilarity = DBL_MAX;
            double previous_dissimilarity = DBL_MAX;

            while (true)
            {
                clear_clusters(clusters);
                k_means<T>::assign_observations(clusters, observations);

                auto empty_cluster_iter = find_empty_cluster(clusters);

                if (empty_cluster_iter != clusters.end())
                {
                    auto& empty_cluster = clusters[empty_cluster_iter - clusters.begin()];

                    std::uniform_int_distribution<size_t> indices_distribution(0, observations.size() - 1);
                    size_t index = indices_distribution(this->random_engine);

                    empty_cluster.mean = observations.at(indices_distribution(this->random_engine));
                    continue;
                }

                for (size_t i = 0; i < clusters.size(); i++)
                {
                    auto& cluster = clusters[i];

                    previous_means[i] = cluster.mean;
                    cluster.mean = compute_medoid(clusters[i]);
                }

                profile.iterations++;

                previous_dissimilarity = current_dissimilarity;
                current_dissimilarity = dissimilarity(clusters);

                if (current_dissimilarity >= previous_dissimilarity)
                    break;
            }

            return clusters;
        }
    };
}