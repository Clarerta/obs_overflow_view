// Overflow View - OBS Studio dock plugin (Windows)
// Показывает текущую сцену целиком, включая содержимое за краями холста.

#include <obs-module.h>
#include <obs-frontend-api.h>
#include <graphics/graphics.h>
#include <graphics/vec4.h>

#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QSpinBox>
#include <QShowEvent>
#include <QResizeEvent>

#include <algorithm>
#include <atomic>

OBS_DECLARE_MODULE()

#ifndef _WIN32
#error "Эта версия плагина написана только для Windows."
#endif

class DisplayWidget : public QWidget {
public:
	std::atomic<int> pad{1000};

	explicit DisplayWidget(QWidget *parent = nullptr) : QWidget(parent)
	{
		setAttribute(Qt::WA_PaintOnScreen);
		setAttribute(Qt::WA_StaticContents);
		setAttribute(Qt::WA_NoSystemBackground);
		setAttribute(Qt::WA_OpaquePaintEvent);
		setAttribute(Qt::WA_DontCreateNativeAncestors);
		setAttribute(Qt::WA_NativeWindow);
		setMinimumSize(240, 160);
	}

	~DisplayWidget() override { destroyDisplay(); }

	QPaintEngine *paintEngine() const override { return nullptr; }

protected:
	void showEvent(QShowEvent *e) override
	{
		QWidget::showEvent(e);
		createDisplay();
	}

	void resizeEvent(QResizeEvent *e) override
	{
		QWidget::resizeEvent(e);
		if (display) {
			qreal dpr = devicePixelRatioF();
			obs_display_resize(display, (uint32_t)(width() * dpr), (uint32_t)(height() * dpr));
		}
	}

private:
	obs_display_t *display = nullptr;

	void createDisplay()
	{
		if (display)
			return;

		qreal dpr = devicePixelRatioF();

		gs_init_data info = {};
		info.cx = (uint32_t)(width() * dpr);
		info.cy = (uint32_t)(height() * dpr);
		info.format = GS_BGRA;
		info.zsformat = GS_ZS_NONE;
		info.window.hwnd = reinterpret_cast<void *>(winId());

		display = obs_display_create(&info, 0x1b1b1f);
		if (display)
			obs_display_add_draw_callback(display, Draw, this);
	}

	void destroyDisplay()
	{
		if (display) {
			obs_display_remove_draw_callback(display, Draw, this);
			obs_display_destroy(display);
			display = nullptr;
		}
	}

	static void DrawCanvasOutline(float w, float h)
	{
		gs_effect_t *solid = obs_get_base_effect(OBS_EFFECT_SOLID);
		gs_eparam_t *colorParam = gs_effect_get_param_by_name(solid, "color");
		gs_technique_t *tech = gs_effect_get_technique(solid, "Solid");

		vec4 color;
		vec4_set(&color, 1.0f, 0.0f, 1.0f, 1.0f);
		gs_effect_set_vec4(colorParam, &color);

		gs_render_start(true);
		gs_vertex2f(0.0f, 0.0f);
		gs_vertex2f(w, 0.0f);
		gs_vertex2f(w, h);
		gs_vertex2f(0.0f, h);
		gs_vertex2f(0.0f, 0.0f);
		gs_vertbuffer_t *vb = gs_render_save();

		gs_technique_begin(tech);
		gs_technique_begin_pass(tech, 0);
		gs_load_vertexbuffer(vb);
		gs_load_indexbuffer(nullptr);
		gs_draw(GS_LINESTRIP, 0, 0);
		gs_technique_end_pass(tech);
		gs_technique_end(tech);

		gs_vertexbuffer_destroy(vb);
	}

	static void Draw(void *data, uint32_t cx, uint32_t cy)
	{
		auto *self = static_cast<DisplayWidget *>(data);

		obs_video_info ovi;
		if (!obs_get_video_info(&ovi) || cx == 0 || cy == 0)
			return;

		float W = (float)ovi.base_width;
		float H = (float)ovi.base_height;
		float pad = (float)self->pad.load();

		float totW = W + 2.0f * pad;
		float totH = H + 2.0f * pad;

		float scale = std::min((float)cx / totW, (float)cy / totH);
		float vw = totW * scale;
		float vh = totH * scale;
		int vx = (int)(((float)cx - vw) / 2.0f);
		int vy = (int)(((float)cy - vh) / 2.0f);

		gs_viewport_push();
		gs_projection_push();
		gs_blend_state_push();
		gs_reset_blend_state();

		gs_set_viewport(vx, vy, (int)vw, (int)vh);
		gs_ortho(-pad, W + pad, -pad, H + pad, -100.0f, 100.0f);

		obs_source_t *scene = obs_frontend_get_current_scene();
		if (scene) {
			obs_source_video_render(scene);
			obs_source_release(scene);
		}

		DrawCanvasOutline(W, H);

		gs_blend_state_pop();
		gs_projection_pop();
		gs_viewport_pop();
	}
};

static void create_dock()
{
	auto *container = new QWidget();
	auto *layout = new QVBoxLayout(container);
	layout->setContentsMargins(4, 4, 4, 4);

	auto *row = new QHBoxLayout();
	row->addWidget(new QLabel(QString::fromUtf8("Запас с каждой стороны (px):")));
	auto *spin = new QSpinBox();
	spin->setRange(100, 20000);
	spin->setSingleStep(100);
	spin->setValue(1000);
	row->addWidget(spin);
	row->addStretch();
	layout->addLayout(row);

	auto *display = new DisplayWidget();
	layout->addWidget(display, 1);

	QObject::connect(spin, &QSpinBox::valueChanged, [display](int v) { display->pad = v; });

	obs_frontend_add_dock_by_id("overflow_view_dock", "\xD0\x9F\xD1\x80\xD0\xBE\xD1\x81\xD0\xBC\xD0\xBE\xD1\x82\xD1\x80 \xD0\xB7\xD0\xB0 \xD0\xBA\xD1\x80\xD0\xB0\xD1\x8F\xD0\xBC\xD0\xB8", container);
}

static void on_frontend_event(enum obs_frontend_event event, void *)
{
	if (event == OBS_FRONTEND_EVENT_FINISHED_LOADING)
		create_dock();
}

bool obs_module_load(void)
{
	obs_frontend_add_event_callback(on_frontend_event, nullptr);
	return true;
}

void obs_module_unload(void)
{
	obs_frontend_remove_event_callback(on_frontend_event, nullptr);
}

const char *obs_module_name(void)
{
	return "Overflow View";
}

const char *obs_module_description(void)
{
	return "Dock that shows the full scene including content outside the canvas.";
}
