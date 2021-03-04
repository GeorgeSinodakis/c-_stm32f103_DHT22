#include <stdint.h>
#include <string>
#include "nvic.h"
#include "tim2345.h"
#include "delay.h"
#include "rcc.h"
#include "gpio.h"
#include "st7735.h"

typedef uint64_t u64;
typedef uint32_t u32;
typedef uint16_t u16;
typedef uint8_t u8;

volatile u16 prev_time = 0, ind, pulses[60];

class Tag
{
public:
	std::string label;
	std::string str_value;
	std::string units;
	float value;
	uint8_t label_font, value_font;
	uint16_t label_color, value_color, back_color;
	uint8_t x, y;
	Tag(std::string label, std::string units, uint8_t label_font, uint8_t value_font, uint16_t label_color, uint16_t value_color, uint16_t back_color, uint8_t x, uint8_t y)
	{
		this->label = label;
		this->units = units;
		this->label_font = label_font;
		this->value_font = value_font;
		this->label_color = label_color;
		this->value_color = value_color;
		this->back_color = back_color;
		this->x = x;
		this->y = y;

		draw_label();
	}
	void update(float new_value)
	{
		if (new_value != value)
		{
			value = new_value;
			draw_value();
		}
	}
	void draw_label()
	{
		st7735_print_str(x, y, label_font, label_color, back_color, &label);
	}
	void draw_value()
	{
		//erase the last value
		st7735_print_str(x, y + label_font * 8 + 5, value_font, back_color, back_color, &str_value);

		str_value = std::to_string(this->value);
		str_value = str_value.substr(0, str_value.find(".") + 2);
		str_value += units;
		st7735_print_str(x, y + label_font * 8 + 5, value_font, value_color, back_color, &str_value);
	}
};

extern "C" void TIM2_IRQHandler()
{
	if (GPIOA_read(0))
		TIM2_CCER |= (1 << 1);
	else
	{
		TIM2_CCER &= ~(1 << 1);
		pulses[ind++] = TIM2_CCR1 - prev_time;
	}
	prev_time = TIM2_CCR1;
}

bool measure(u16 *humid, u16 *temp)
{
	ind = 0;
	TIM2_SR &= ~(1 << 1);
	TIM2_CCER |= 1 << 1; //capture on falling edge

	//trigger
	GPIOA_clear(0);
	ms(10);
	GPIOA_set(0);

	//measure
	NVIC_ISER0 |= 1 << 28; //enable interrupt
	ms(2000);
	NVIC_ICER0 |= 1 << 28; //disable interrupt

	//safety feature
	if (ind < 40)
		return 1;

	//extract information
	u8 bit, checksum = 0;
	*temp = 0;
	*humid = 0;
	bit = 0;
	while (bit < 8)
	{
		if (pulses[--ind] > 40)
			checksum |= (1 << bit);
		bit++;
	}
	bit = 0;
	while (bit < 16)
	{
		if (pulses[--ind] > 40)
			*temp |= (1 << bit);
		bit++;
	}
	bit = 0;
	while (bit < 16)
	{
		if (pulses[--ind] > 40)
			*humid |= (1 << bit);
		bit++;
	}

	//calculate checksum
	bit = (*humid) & 0xff;
	bit += (*humid >> 8) & 0xff;
	bit += (*temp) & 0xff;
	bit += (*temp >> 8) & 0xff;
	if (bit != checksum)
		return 1;

	return 0;
}

int main(void)
{
	clock_72Mhz();

	GPIOA_clock_enable();
	GPIOA_mode(OUTPUT_OPENDRAIN, 0);
	GPIOA_set(0);

	RCC_APB1ENR |= 1 << 0;
	TIM2_PSC = 71; //TICK EVERY 1 uS
	TIM2_CCMR1 = 1 << 0;
	TIM2_CCMR1 |= 1 << 4 | 1 << 5;
	TIM2_CCER = (1 << 1); //capture on falling edge
	TIM2_CCER |= 1 << 0;  //enable capture
	TIM2_DIER = 1 << 1;	  //enable interrupt
	TIM2_CR1 = 1 << 0;

	st7735_init(3);
	st7735_fill(WHITE);

	u16 humid = 0, temp = 0;

	Tag HumTag("Hum:", "%", 2, 2, BLACK, RED, WHITE, 10, 10);
	Tag TempTag("Temp:", "C", 2, 2, BLACK, RED, WHITE, 10, 70);

	while (1)
	{
		measure(&humid, &temp);
		TempTag.update(((float)temp) / 10.0f + 0.1f * (float)(temp % 10));
		HumTag.update(((float)humid) / 10.0f + 0.1f * (float)(humid % 10));
	}
}