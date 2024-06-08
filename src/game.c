#include "tiny3d.h"

double accumulated_time = 0.0;
double interpolant;
#define TICK_RATE 20.0
#define SEC_PER_TICK (1.0 / TICK_RATE)

float mouse_sensitivity = 0.1f;

#define SCREEN_WIDTH 256
#define SCREEN_HEIGHT 256
color_t screen_pixels[SCREEN_WIDTH*SCREEN_HEIGHT];
float screen_depth[SCREEN_WIDTH*SCREEN_HEIGHT];
framebuffer_t screen = {
	.width = SCREEN_WIDTH,
	.height = SCREEN_HEIGHT,
	.pixels = screen_pixels,
	.depth = screen_depth
};

image_t sponge;

void keydown(int scancode){
	switch (scancode){
		case 27: exit(0); break;
		case 'P': toggle_fullscreen(); break;
		case 'C': lock_mouse(!is_mouse_locked()); break;
	}
}

void keyup(int scancode){
	switch (scancode){
	}
}

void mousemove(int x, int y){
	
}

void tick(){
	
}

void update(double time, double deltaTime, int nAudioFrames, int16_t *audioSamples){
	accumulated_time += deltaTime;
	while (accumulated_time >= 1.0/20.0){
		accumulated_time -= 1.0/20.0;
		tick();
	}
	interpolant = accumulated_time / SEC_PER_TICK;

	t3d_clear((color_t){255,0,0,255});
	t3d_perspective(0.5f*M_PI,(float)screen.width/screen.height,0.01f,100.0f);
	t3d_load_identity();
	t3d_translate(0,0,-1.25);
	t3d_rotate(0,1,0,0.5f*M_PI*sin(time));
	t3d_translate(-0.5f,-0.5f,0.0f);
	t3d_position(0,0,0); t3d_texcoord(0,0);
	t3d_position(1,0,0); t3d_texcoord(1,0);
	t3d_position(1,1,0); t3d_texcoord(1,1);
	t3d_position(1,1,0); t3d_texcoord(1,1);
	t3d_position(0,1,0); t3d_texcoord(0,1);
	t3d_position(0,0,0); t3d_texcoord(0,0);
}

int main(int argc, char **argv){
	t3d_set_framebuffer(&screen);
	sponge.pixels = load_image(true,&sponge.width,&sponge.height,"blocks.png");
	t3d_set_texture(&sponge);
    open_window((image_t *)&screen,3);
}