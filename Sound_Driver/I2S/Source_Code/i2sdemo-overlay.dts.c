/dts-v1/;
/plugin/;

/ {
	compatible = "brcm,bcm2835";

	/* enable the Pi I2S controller (same as the stock overlay) */
	fragment@0 {
		target = <&i2s_clk_producer>;
		__overlay__ {
			status = "okay";
		};
	};

	/* our own codec node and card node */
	fragment@1 {
		target-path = "/";
		__overlay__ {
			i2sdemo_codec: i2sdemo-codec {
				compatible = "student,i2sdemo-codec";
				#sound-dai-cells = <0>;
				status = "okay";
			};

			i2sdemo-card {
				compatible = "student,i2sdemo-card";
				card-name = "i2sdemo";
				i2s-controller = <&i2s_clk_producer>;
				audio-codec = <&i2sdemo_codec>;
				status = "okay";
			};
		};
	};
};