/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2025 Scott Moreau
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <cmath>
#include <wayfire/core.hpp>
#include <wayfire/seat.hpp>
#include <wayfire/plugin.hpp>
#include <wayfire/view-helpers.hpp>
#include <wayfire/toplevel-view.hpp>
#include <wayfire/util/duration.hpp>
#include <wayfire/view-transform.hpp>
#include <wayfire/render-manager.hpp>
#include <wayfire/window-manager.hpp>
#include <wayfire/signal-definitions.hpp>

namespace wf
{
namespace dodge
{
static std::string dodge_transformer_from = "dodge_transformer_from";
static std::string dodge_transformer_to = "dodge_transformer_to";
class wayfire_dodge : public wf::plugin_interface_t
{
    wayfire_view view_from, view_to, last_focused_view;
    std::shared_ptr<wf::scene::view_2d_transformer_t> tr_from, tr_to;
    wf::animation::simple_animation_t progression{wf::create_option(2000)};
    bool view_to_focused;
    wf::pointf_t direction;

  public:
    void init() override
    {
        wf::get_core().connect(&view_mapped);
        wf::get_core().connect(&view_unmapped);
        this->progression.set(0, 0);
    }

    wf::signal::connection_t<wf::view_activated_state_signal> view_activated =
        [=] (wf::view_activated_state_signal *ev)
    {
        if (ev->view == wf::get_core().seat->get_active_view())
        {
            last_focused_view = wf::get_core().seat->get_active_view();
            return;
        }
		if (!progression.running())
		{
            view_from = last_focused_view;
            view_to = ev->view;
        }
        if (!view_from || !view_to || view_from == view_to || progression.running())
        {
            return;
        }
        view_bring_to_front(view_from);
        if (!view_from->get_transformed_node()->get_transformer<wf::scene::view_2d_transformer_t>(dodge_transformer_from))
        {
            tr_from = std::make_shared<wf::scene::view_2d_transformer_t>(view_from);
            view_from->get_transformed_node()->add_transformer(tr_from, wf::TRANSFORMER_2D, dodge_transformer_from);
            view_from->get_output()->render->add_effect(&dodge_animation_hook, wf::OUTPUT_EFFECT_PRE);
        }
        if (!view_to->get_transformed_node()->get_transformer<wf::scene::view_2d_transformer_t>(dodge_transformer_to))
        {
            tr_to = std::make_shared<wf::scene::view_2d_transformer_t>(view_to);
            view_to->get_transformed_node()->add_transformer(tr_to, wf::TRANSFORMER_2D, dodge_transformer_to);
            view_to->get_output()->render->add_effect(&dodge_animation_hook, wf::OUTPUT_EFFECT_PRE);
        }
        compute_direction();
        view_to_focused = false;
        this->progression.animate(0, 1);
    };

    wf::signal::connection_t<wf::view_mapped_signal> view_mapped =
        [=] (wf::view_mapped_signal *ev)
    {
        ev->view->connect(&view_activated);
    };

    wf::signal::connection_t<wf::view_unmapped_signal> view_unmapped =
        [=] (wf::view_unmapped_signal *ev)
    {
        last_focused_view = wf::get_core().seat->get_active_view();
        if (ev->view == view_from)
        {
            view_from = nullptr;
        }
        if (ev->view == view_to)
        {
            view_to = nullptr;
        }
    };

    double magnitude(int x, int y) {
        return std::sqrt(x * x + y * y);
    }

    void compute_direction()
    {
        auto from_bb = view_from->get_bounding_box();
        auto to_bb = view_to->get_bounding_box();
        auto from_center = wf::point_t{from_bb.x + from_bb.width / 2, from_bb.y + from_bb.height / 2};
        auto to_center = wf::point_t{to_bb.x + to_bb.width / 2, to_bb.y + to_bb.height / 2};
        auto x = double(from_center.x - to_center.x);
        auto y = double(from_center.y - to_center.y);
        auto m = magnitude(x, y);
        x /= m;
        y /= m;
        direction = wf::pointf_t{std::asin(x), std::asin(y)};
    }

    void damage_views()
    {
        view_from->damage();
        view_to->damage();
    }

    void finish_animation()
    {
        if (view_from)
        {
            view_from->get_output()->render->rem_effect(&dodge_animation_hook);
            view_from->get_transformed_node()->rem_transformer(dodge_transformer_from);
        }
        if (view_to)
        {
            view_to->get_output()->render->rem_effect(&dodge_animation_hook);
            view_to->get_transformed_node()->rem_transformer(dodge_transformer_to);
        }
        view_from = nullptr;
        view_to = nullptr;
        tr_from = nullptr;
        tr_to = nullptr;
    }

    bool step_animation()
    {
        auto from_bb = view_from->get_bounding_box();
        auto to_bb = view_to->get_bounding_box();
        auto move_dist_x = std::min(from_bb.width, to_bb.width) * direction.x * 0.5;
        auto move_dist_y = std::min(from_bb.height, to_bb.height) * direction.y * 0.5;
        tr_from->translation_x = std::sin(progression.progress() * M_PI) * move_dist_x;
        tr_from->translation_y = std::sin(progression.progress() * M_PI) * move_dist_y;
        tr_to->translation_x = std::sin(-progression.progress() * M_PI) * move_dist_x;
        tr_to->translation_y = std::sin(-progression.progress() * M_PI) * move_dist_y;
        if (progression.progress() > 0.5 && !view_to_focused)
        {
            wf::get_core().seat->focus_view(view_to);
            view_bring_to_front(view_to);
            view_to_focused = true;
        }
        return progression.running();
    }

    wf::effect_hook_t dodge_animation_hook = [=] ()
    {
        damage_views();
        bool result = step_animation();
        damage_views();

        if (!result)
        {
            finish_animation();
        }
    };

    void fini() override
    {
        if (view_from)
        {
            view_from->get_output()->render->rem_effect(&dodge_animation_hook);
            view_from->get_transformed_node()->rem_transformer(dodge_transformer_from);
        }
        if (view_to)
        {
            view_to->get_output()->render->rem_effect(&dodge_animation_hook);
            view_to->get_transformed_node()->rem_transformer(dodge_transformer_to);
        }
    }
};
}
}

DECLARE_WAYFIRE_PLUGIN(wf::dodge::wayfire_dodge);
