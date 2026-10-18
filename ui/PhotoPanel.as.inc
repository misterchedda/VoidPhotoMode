      // BEGIN GENERATED CONTRACT
      private static const PM_FovUp:int = 1;
      private static const PM_FovDown:int = 2;
      private static const PM_ResetView:int = 3;
      private static const PM_ReturnGame:int = 4;
      private static const PM_RollUp:int = 5;
      private static const PM_RollDown:int = 6;
      private static const PM_SpeedUp:int = 7;
      private static const PM_SpeedDown:int = 8;
      private static const PM_ToggleThirds:int = 9;
      private static const PM_ToggleBloom:int = 10;
      private static const PM_ToggleGrading:int = 11;
      private static const PM_ToggleDof:int = 12;
      private static const PM_ExposureDown:int = 13;
      private static const PM_ExposureUp:int = 14;
      private static const PM_ToggleManual:int = 15;
      private static const PM_DistanceDown:int = 16;
      private static const PM_DistanceUp:int = 17;
      private static const PM_WidthDown:int = 18;
      private static const PM_WidthUp:int = 19;
      private static const PM_ResetEffects:int = 20;
      private static const PM_BloomDown:int = 21;
      private static const PM_BloomUp:int = 22;
      private static const PM_BokehDown:int = 23;
      private static const PM_BokehUp:int = 24;
      private static const PM_SaturationDown:int = 25;
      private static const PM_SaturationUp:int = 26;
      private static const PM_ContrastDown:int = 27;
      private static const PM_ContrastUp:int = 28;
      private static const PM_WarmthDown:int = 29;
      private static const PM_WarmthUp:int = 30;
      private static const PM_TintDown:int = 31;
      private static const PM_TintUp:int = 32;
      private static const PM_FovSlider:int = 33;
      private static const PM_RollSlider:int = 34;
      private static const PM_SpeedSlider:int = 35;
      private static const PM_ExposureSlider:int = 36;
      private static const PM_DistanceSlider:int = 37;
      private static const PM_WidthSlider:int = 38;
      private static const PM_BloomSlider:int = 39;
      private static const PM_BokehSlider:int = 40;
      private static const PM_SaturationSlider:int = 41;
      private static const PM_ContrastSlider:int = 42;
      private static const PM_WarmthSlider:int = 43;
      private static const PM_TintSlider:int = 44;
      private static const PM_TakePhoto:int = 45;
      private static const PM_HideUi:int = 46;
      private static const PM_AssetScale:int = 64;
      private static const PM_ValueScale:int = 32768;
      private static const PM_ValueSteps:int = 1000;
      // END GENERATED CONTRACT
      // Local HUD extension. Uses the game's embedded fonts and shared bitmap symbols.
      private var _pmPanel:Sprite;
      private var _pmCursor:Sprite;
      private var _pmFov:flash.text.TextField;
      private var _pmRoll:flash.text.TextField;
      private var _pmSpeed:flash.text.TextField;
      private var _pmThirds:flash.text.TextField;
      private var _pmGuide:Sprite;
      private var _pmHint:flash.text.TextField;
      private var _pmSaved:Array;
      private var _pmButtons:Array;
      private var _pmWasActive:Boolean = false;
      private var _pmAssets:int = 0;
      private var _pmFonts:Array;
      private var _pmPage:int = 0;
      private var _pmPageItems:Array;
      private var _pmBloom:flash.text.TextField;
      private var _pmGrade:flash.text.TextField;
      private var _pmDof:flash.text.TextField;
      private var _pmExposure:flash.text.TextField;
      private var _pmManual:flash.text.TextField;
      private var _pmDistance:flash.text.TextField;
      private var _pmWidth:flash.text.TextField;
      private var _pmReset:Object;
      private var _pmBloomStrength:flash.text.TextField;
      private var _pmBokeh:flash.text.TextField;
      private var _pmSaturation:flash.text.TextField;
      private var _pmContrast:flash.text.TextField;
      private var _pmWarmth:flash.text.TextField;
      private var _pmTint:flash.text.TextField;
      private var _pmSliders:Array;
      private var _pmDrag:Object;

      private function _PM_Bitmap(name:String):Sprite
      {
         // Use the game's already-compiled factory and its resource bindings.
         // No pixel upload, quantization, or per-pixel Graphics reconstruction.
         var clipName:String = "";
         var frame:int = 1;
         if(name == "pm_arrow" || name == "pm_arrow_glow")
         {
            clipName = "lib_common_fla.arrowStates_32";
            if(name == "pm_arrow_glow") frame = 14;
         }
         if(name == "pm_cursor") clipName = "lib_mouseCursor";
         if(name == "pm_valueplate") name = "m_btn0_M";
         if(name == "pm_valueborder") return null;
         var display:flash.display.DisplayObject;
         if(clipName != "")
         {
            var clip:MovieClip = DynamicInstance.newInstance(clipName) as MovieClip;
            if(!clip) return null;
            clip.gotoAndStop(frame);
            display = clip;
         }
         else display = DynamicInstance.getBitmapFromClass(name);
         if(!display) return null;
         var sprite:Sprite = new Sprite();
         var bounds:flash.geom.Rectangle = display.getBounds(display);
         display.x = -bounds.x;
         display.y = -bounds.y;
         sprite.addChild(display);
         return sprite;
      }

      private function _PM_Label(parent:Sprite, text:String, x:Number, y:Number, size:Number, width:Number, style:int = 0, color:uint = 0xdde3db):flash.text.TextField
      {
         var label:flash.text.TextField = new flash.text.TextField();
         var font:flash.text.Font = _pmFonts[style] as flash.text.Font;
         var format:flash.text.TextFormat = new flash.text.TextFormat(font ? font.fontName : "Arial",size,color);
         if(style == 1) format.letterSpacing = -1;
         label.defaultTextFormat = format;
         label.embedFonts = font != null;
         label.selectable = false;
         label.mouseEnabled = false;
         label.width = width;
         label.height = size + 24;
         label.x = x;
         label.y = y;
         label.text = text;
         parent.addChild(label);
         return label;
      }

      // Three-part strips keep the original distressed end caps intact.
      private function _PM_Strip(parent:Sprite, prefix:String, x:Number, y:Number, width:Number, height:Number, paper:Boolean, bit:int):Sprite
      {
         var strip:Sprite = new Sprite();
         strip.x = x;
         strip.y = y;
         var left:Sprite = _PM_Bitmap(prefix + "_L");
         var middle:Sprite = _PM_Bitmap(prefix + "_M");
         var right:Sprite = _PM_Bitmap(prefix + "_R");
         if(left && middle && right)
         {
            var cap:Number = Math.min(width * 0.28,height * 2);
            left.width = cap;
            left.height = height;
            middle.x = cap - 1;
            middle.width = width - cap * 2 + 2;
            middle.height = height;
            right.x = width - cap;
            right.width = cap;
            right.height = height;
            strip.addChild(left);
            strip.addChild(middle);
            strip.addChild(right);
            _pmAssets |= bit;
         }
         else
         {
            strip.graphics.beginFill(0xc4d2cc);
            strip.graphics.moveTo(0,5);
            strip.graphics.lineTo(width - 8,0);
            strip.graphics.lineTo(width,height - 9);
            strip.graphics.lineTo(width - 22,height);
            strip.graphics.lineTo(8,height - 4);
            strip.graphics.lineTo(15,height * 0.5);
            strip.graphics.lineTo(0,5);
            strip.graphics.endFill();
         }
         strip.transform.colorTransform = paper ? new flash.geom.ColorTransform(0.32,0.34,0.33,1,150,158,153,0) : new flash.geom.ColorTransform(0.12,0.10,0.12,0.92,5,5,7,0);
         parent.addChild(strip);
         return strip;
      }

      private function _PM_Button(text:String, action:Number, x:Number, y:Number, width:Number, style:int = 0):void
      {
         var button:Sprite = new Sprite();
         button.x = x;
         button.y = y;
         _PM_Strip(button,"m_btn0",0,0,width,58,false,16);
         var selected:Sprite = _PM_Strip(button,"m_btn1",-6,-2,width + 12,62,true,32);
         selected.alpha = 0;
         var label:flash.text.TextField = _PM_Label(button,text,action < 0 ? 16 : 24,10,action < 0 ? 24 : 26,width - (action < 0 ? 28 : 44),style);
         _pmPanel.addChild(button);
         _pmButtons.push({view:button,action:action,w:width,h:58,on:selected,label:label,amount:0,page:-1});
      }

      // Original control-bar artwork and keyboard keycaps from the game's libraries.
      private function _PM_ControlBacking(parent:Sprite, width:Number, selected:Boolean):Sprite
      {
         var view:Sprite = new Sprite();
         var suffix:String = selected ? "_ON" : "";
         var left:Sprite = _PM_Bitmap("ctrlBar_T0_L" + suffix);
         var middle:Sprite = _PM_Bitmap("ctrlBar_T0_M" + suffix);
         var right:Sprite = _PM_Bitmap("ctrlBar_T0_R1" + suffix);
         if(left && middle && right)
         {
            left.width = 36; left.height = 52;
            middle.x = 35; middle.width = width - 70; middle.height = 52;
            right.x = width - 36; right.width = 36; right.height = 52;
            view.addChild(left); view.addChild(middle); view.addChild(right);
         }
         parent.addChild(view);
         return view;
      }

      private function _PM_KeyButton(text:String, key:String, action:Number, x:Number, y:Number, width:Number):void
      {
         var button:Sprite = new Sprite(); button.x = x; button.y = y;
         _PM_ControlBacking(button,width,false);
         var selected:Sprite = _PM_ControlBacking(button,width,true); selected.alpha = 0;
         var keycap:Sprite;
         if(key == "Esc") keycap = _PM_Bitmap("keyboard_ESC_s");
         else
         {
            var clip:MovieClip = DynamicInstance.newInstance("keyboardGenericKey") as MovieClip;
            if(clip)
            {
               clip.stop();
               clip["label"].fontSize = Number(clip["label"].fontSize) * 0.70;
               clip["label"].text = key;
               keycap = new Sprite();
               var bounds:flash.geom.Rectangle = clip.getBounds(clip);
               clip.x = -bounds.x; clip.y = -bounds.y;
               keycap.addChild(clip);
            }
         }
         if(keycap)
         {
            var keyScale:Number = Math.min(48 / keycap.width,46 / keycap.height);
            keycap.scaleX = keycap.scaleY = keyScale;
            keycap.x = 32 - keycap.width * 0.5; keycap.y = 26 - keycap.height * 0.5;
            button.addChild(keycap);
         }
         else _PM_Label(button,key,8,10,20,50,0);
         var label:flash.text.TextField = _PM_Label(button,text,64,10,26,width - 72,2);
         _pmPanel.addChild(button);
         _pmButtons.push({view:button,action:action,w:width,h:52,on:selected,label:label,amount:0,page:-1});
      }

      private function _PM_Arrow(action:Number, x:Number, y:Number, right:Boolean):void
      {
         var button:Sprite = new Sprite();
         button.x = x; button.y = y;
         var icon:Sprite = _PM_Bitmap("pm_arrow");
         var glow:Sprite = _PM_Bitmap("pm_arrow_glow");
         var selected:Sprite = new Sprite();
         if(icon && glow)
         {
            icon.width = 42; icon.height = 37;
            icon.x = -21; icon.y = -18.5;
            glow.width = 70; glow.height = 67;
            glow.x = -35; glow.y = -33.5;
            var normal:Sprite = new Sprite(); normal.x = 28; normal.y = 29;
            normal.rotation = right ? 90 : -90;
            normal.addChild(icon); button.addChild(normal);
            selected.x = 28; selected.y = 29;
            selected.rotation = right ? 90 : -90;
            selected.addChild(glow);
            _pmAssets |= 256;
         }
         else
         {
            _PM_Label(button,right ? ">" : "<",12,6,32,44);
         }
         button.addChild(selected); selected.alpha = 0;
         _pmPanel.addChild(button);
         _pmButtons.push({view:button,action:action,w:56,h:54,on:selected,label:null,amount:0,page:0});
      }

      private function _PM_Row(text:String, down:Number, up:Number, y:Number, page:int = 0):flash.text.TextField
      {
         var first:int = _pmPanel.numChildren;
         var backing:Sprite = new Sprite();
         var matrix:flash.geom.Matrix = new flash.geom.Matrix();
         matrix.createGradientBox(580,52,0,-16,y + 2);
         backing.graphics.beginGradientFill("linear",[0x08090d,0x08090d,0x08090d,0x08090d],[0,0.72,0.60,0],[0,35,210,255],matrix);
         backing.graphics.drawRect(-16,y + 2,580,52);
         backing.graphics.endFill();
         _pmPanel.addChild(backing);
         _PM_Label(_pmPanel,text,20,y + 10,24,250,0);
         var plate:Sprite = _PM_Bitmap("pm_valueplate");
         if(plate)
         {
            plate.x = 338; plate.y = y + 8;
            plate.width = 132; plate.height = 44;
            _pmPanel.addChild(plate);
         }
         var label:flash.text.TextField = _PM_Label(_pmPanel,"",344,y + 8,28,120,3,0xdde9e0);
         var centered:flash.text.TextFormat = label.defaultTextFormat;
         centered.align = "center"; label.defaultTextFormat = centered;
         _PM_Arrow(down,276,y,false);
         _PM_Arrow(up,476,y,true);
         _pmButtons[_pmButtons.length - 1].page = page;
         _pmButtons[_pmButtons.length - 2].page = page;
         for(var i:int = first; i < _pmPanel.numChildren; ++i)
            _pmPageItems.push({view:_pmPanel.getChildAt(i),page:page});
         return label;
      }

      private function _PM_Slider(command:int, y:Number, page:int, low:Number, high:Number, logarithmic:Boolean = false):void
      {
         var view:Sprite = new Sprite(); view.x = 338; view.y = y + 48;
         _pmPanel.addChild(view);
         _pmSliders.push({view:view,command:command,y:y,page:page,low:low,high:high,logarithmic:logarithmic});
         _pmPageItems.push({view:view,page:page});
      }

      private function _PM_Create():void
      {
         _pmFonts = [com.bsp.font.NormalFont.instance,com.bsp.font.TitleFont.instance,com.bsp.font.HandWrittenElegantFont.instance,com.bsp.font.NormalFontSmallCaps.instance];
         for(var f:int = 0; f < 4; ++f) if(_pmFonts[f]) _pmAssets |= 1 << f;
         _pmPanel = new Sprite();
         _pmPanel.x = 1312;
         _pmPanel.y = 492;
         // The header uses lowercase suffixes in the original library.
         var header:Sprite = new Sprite();
         var hl:Sprite = _PM_Bitmap("header1_l");
         var hm:Sprite = _PM_Bitmap("header1_m");
         var hr:Sprite = _PM_Bitmap("header1_r");
         if(hl && hm && hr)
         {
            hl.width = 62; hl.height = 58;
            hm.x = 61; hm.width = 350; hm.height = 58;
            hr.x = 410; hr.width = 164; hr.height = 58;
            header.addChild(hl); header.addChild(hm); header.addChild(hr);
            _pmAssets |= 64;
         }
         else _PM_Strip(header,"m_btn1",0,0,574,58,true,32);
         header.x = -18;
         _pmPanel.addChild(header);
         _PM_Label(_pmPanel,"PHOTO MODE",66,8,31,330,1,0x29272d);
         _pmButtons = [];
         _pmPageItems = [];
         _pmSliders = [];
         _PM_Button("Camera",-1,0,76,134);
         _PM_Button("Effects",-2,142,76,134);
         _PM_Button("Focus",-3,284,76,134);
         _PM_Button("Color",-4,426,76,134);
         _pmFov = _PM_Row("Field of view",PM_FovDown,PM_FovUp,138);
         _pmRoll = _PM_Row("Camera roll",PM_RollDown,PM_RollUp,192);
         _pmSpeed = _PM_Row("Movement speed",PM_SpeedDown,PM_SpeedUp,246);
         _pmThirds = _PM_Row("Thirds guide",PM_ToggleThirds,PM_ToggleThirds,300);
         _pmBloom = _PM_Row("Bloom",PM_ToggleBloom,PM_ToggleBloom,138,1);
         _pmBloomStrength = _PM_Row("Bloom strength",PM_BloomDown,PM_BloomUp,192,1);
         _pmGrade = _PM_Row("Color grading",PM_ToggleGrading,PM_ToggleGrading,246,1);
         _pmDof = _PM_Row("Depth of field",PM_ToggleDof,PM_ToggleDof,300,1);
         _pmExposure = _PM_Row("Exposure",PM_ExposureDown,PM_ExposureUp,354,1);
         _pmManual = _PM_Row("Manual focus",PM_ToggleManual,PM_ToggleManual,138,2);
         _pmDistance = _PM_Row("Focus distance",PM_DistanceDown,PM_DistanceUp,192,2);
         _pmWidth = _PM_Row("Focus width",PM_WidthDown,PM_WidthUp,246,2);
         _pmBokeh = _PM_Row("Bokeh size",PM_BokehDown,PM_BokehUp,300,2);
         _pmSaturation = _PM_Row("Saturation",PM_SaturationDown,PM_SaturationUp,138,3);
         _pmContrast = _PM_Row("Contrast",PM_ContrastDown,PM_ContrastUp,192,3);
         _pmWarmth = _PM_Row("Warmth",PM_WarmthDown,PM_WarmthUp,246,3);
         _pmTint = _PM_Row("Tint",PM_TintDown,PM_TintUp,300,3);
         // BEGIN GENERATED SLIDERS
         _PM_Slider(PM_FovSlider,138,0,5.0,150.0,false);
         _PM_Slider(PM_RollSlider,192,0,-180.0,180.0,false);
         _PM_Slider(PM_SpeedSlider,246,0,0.1,16.0,true);
         _PM_Slider(PM_ExposureSlider,354,1,-5.0,5.0,false);
         _PM_Slider(PM_DistanceSlider,192,2,0.1,1000.0,true);
         _PM_Slider(PM_WidthSlider,246,2,0.05,100.0,true);
         _PM_Slider(PM_BloomSlider,192,1,0.0,4.0,false);
         _PM_Slider(PM_BokehSlider,300,2,0.0,4.0,false);
         _PM_Slider(PM_SaturationSlider,138,3,0.0,2.0,false);
         _PM_Slider(PM_ContrastSlider,192,3,0.5,1.5,false);
         _PM_Slider(PM_WarmthSlider,246,3,-1.0,1.0,false);
         _PM_Slider(PM_TintSlider,300,3,-1.0,1.0,false);
         // END GENERATED SLIDERS
         _PM_KeyButton("Take photo","F10",PM_TakePhoto,0,410,272);
         _PM_KeyButton("Hide UI","F9",PM_HideUi,284,410,276);
         _PM_KeyButton("Reset view","F8",PM_ResetView,0,466,272);
         _pmReset = _pmButtons[_pmButtons.length - 1];
         _PM_KeyButton("Return","Esc",PM_ReturnGame,284,466,276);
         _pmHint = _PM_Label(_pmPanel,"",22,524,20,540,2,0xb9c8c4);
         _pmGuide = new Sprite();
         _pmGuide.mouseEnabled = false;
         for(var pass:int = 0; pass < 2; ++pass)
         {
            _pmGuide.graphics.lineStyle(pass == 0 ? 3 : 1,pass == 0 ? 0x000000 : 0xe4e8e2,pass == 0 ? 0.25 : 0.45);
            for(var line:int = 1; line <= 2; ++line)
            {
               _pmGuide.graphics.moveTo(line * 640,0);
               _pmGuide.graphics.lineTo(line * 640,1080);
               _pmGuide.graphics.moveTo(0,line * 360);
               _pmGuide.graphics.lineTo(1920,line * 360);
            }
         }
         _pmGuide.visible = false;
         addChild(_pmGuide);
         addChild(_pmPanel);
         _pmCursor = new Sprite();
         var nativeCursor:Sprite = _PM_Bitmap("pm_cursor");
         if(nativeCursor)
         {
            nativeCursor.width = 32; nativeCursor.height = 40;
            _pmCursor.addChild(nativeCursor);
            _pmAssets |= 128;
         }
         if(_pmCursor.numChildren == 0)
         {
            _pmCursor.graphics.lineStyle(1,0x172124,1);
            _pmCursor.graphics.beginFill(0xd8e5df,1);
            _pmCursor.graphics.moveTo(0,0);
            _pmCursor.graphics.lineTo(7,23);
            _pmCursor.graphics.lineTo(11,14);
            _pmCursor.graphics.lineTo(20,13);
            _pmCursor.graphics.lineTo(0,0);
            _pmCursor.graphics.endFill();
         }
         addChild(_pmCursor);
      }

      public function _PM_Update(active:Boolean, fov:Number, focused:Boolean, shown:Boolean, cx:Number, cy:Number, pressed:Boolean, showHUD:Boolean, roll:Number, speed:Number, thirds:Boolean, effects:Number, exposure:Number, distance:Number, width:Number, bloomStrength:Number, bokeh:Number, saturation:Number, contrast:Number, warmth:Number, tint:Number, held:Boolean, captureStatus:Number, resetPressed:Boolean):Number
      {
         var command:Number = 0;
         var sliderValue:int = 0;
         var i:int;
         var child:flash.display.DisplayObject;
         if(active && !_pmPanel) _PM_Create();
         if(!_pmPanel) return 0;
         if(active && !_pmWasActive)
         {
            _pmPage = 0;
            _pmSaved = [];
            for(i = 0; i < numChildren; ++i)
            {
               child = getChildAt(i);
               if(child != _pmPanel && child != _pmCursor && child != _pmGuide) _pmSaved.push({view:child,visible:child.visible});
            }
         }
         if(active)
         {
            for each(var saved:Object in _pmSaved) saved.view.visible = showHUD ? saved.visible : false;
            _pmFov.text = fov.toFixed(1) + "\u00b0";
            _pmRoll.text = (Math.abs(roll) < 0.05 ? 0 : roll).toFixed(1) + "\u00b0";
            _pmSpeed.text = speed.toFixed(2) + "x";
            _pmThirds.text = thirds ? "On" : "Off";
            var flags:int = int(effects);
            _pmBloom.text = (flags & 1) != 0 ? "On" : "Off";
            _pmGrade.text = (flags & 2) != 0 ? "On" : "Off";
            _pmDof.text = (flags & 4) != 0 ? "On" : "Off";
            _pmExposure.text = (exposure > 0 ? "+" : "") + exposure.toFixed(2);
            _pmManual.text = (flags & 16) == 0 ? "N/A" : (flags & 8) != 0 ? "On" : "Off";
            _pmDistance.text = distance.toFixed(2);
            _pmWidth.text = width.toFixed(2);
            _pmBloomStrength.text = bloomStrength.toFixed(2) + "x";
            _pmBokeh.text = bokeh.toFixed(2) + "x";
            _pmSaturation.text = saturation.toFixed(2);
            _pmContrast.text = contrast.toFixed(2);
            _pmWarmth.text = warmth.toFixed(2);
            _pmTint.text = tint.toFixed(2);
            _pmHint.text = captureStatus == 1 ? "Saving photo..." : captureStatus == 2 ? "Photo saved" : captureStatus == 3 ? "Photo not saved - check capture status" : "Hold RMB  Look    WASD  Move    Wheel  Speed";
            _pmCursor.x = cx;
            _pmCursor.y = cy;
            var nextPage:int = _pmPage;
            _pmReset.action = _pmPage == 0 ? PM_ResetView : PM_ResetEffects;
            _pmReset.label.text = _pmPage == 0 ? "Reset view" : "Reset effects";
            for each(var item:Object in _pmButtons)
            {
               var available:Boolean = item.page < 0 || item.page == _pmPage;
               if(item.action >= PM_ToggleManual && item.action <= PM_WidthUp && (flags & 16) == 0) available = false;
               if(item.action >= PM_DistanceDown && item.action <= PM_WidthUp && (flags & 12) != 12) available = false;
               if((item.action == PM_BloomDown || item.action == PM_BloomUp) && (flags & 33) != 33) available = false;
               if((item.action == PM_BokehDown || item.action == PM_BokehUp) && (flags & 20) != 20) available = false;
               var hovered:Boolean = available && focused && shown && cx >= _pmPanel.x + item.view.x && cx < _pmPanel.x + item.view.x + item.w && cy >= _pmPanel.y + item.view.y && cy < _pmPanel.y + item.view.y + item.h;
               var selected:Boolean = item.action < 0 && -item.action - 1 == _pmPage;
               item.amount += ((hovered || selected ? 1 : 0) - item.amount) * 0.35;
               item.on.alpha = item.amount;
               if(item.label) item.label.textColor = hovered || selected ? 0x24292b : 0xdde3db;
               if(hovered && pressed)
               {
                  if(item.action < 0) nextPage = -item.action - 1;
                  else command = item.action;
               }
            }
            if(!focused || !shown || nextPage != _pmPage) _pmDrag = null;
            var sliderValues:Array = [fov,roll,speed,exposure,distance,width,bloomStrength,bokeh,saturation,contrast,warmth,tint];
            for each(var slider:Object in _pmSliders)
            {
               var canDrag:Boolean = slider.page == _pmPage && focused && shown;
               if((slider.command == PM_DistanceSlider || slider.command == PM_WidthSlider) && (flags & 28) != 28) canDrag = false;
               if(slider.command == PM_BloomSlider && (flags & 33) != 33) canDrag = false;
               if(slider.command == PM_BokehSlider && (flags & 20) != 20) canDrag = false;
               if(_pmDrag == slider && !canDrag) _pmDrag = null;
               var current:Number = sliderValues[slider.command - PM_FovSlider];
               var pos:Number = slider.logarithmic ? Math.log(current / slider.low) / Math.log(slider.high / slider.low) : (current - slider.low) / (slider.high - slider.low);
               pos = Math.max(0,Math.min(1,pos));
               slider.view.graphics.clear();
               slider.view.graphics.lineStyle(1,0x9aadaa,0.55);
               slider.view.graphics.moveTo(0,0); slider.view.graphics.lineTo(132,0);
               slider.view.graphics.lineStyle(3,0xdde9e0,0.9);
               slider.view.graphics.moveTo(pos * 132,-3); slider.view.graphics.lineTo(pos * 132,3);
               if(canDrag && pressed && cx >= _pmPanel.x + 332 && cx <= _pmPanel.x + 476 && cy >= _pmPanel.y + slider.y + 4 && cy <= _pmPanel.y + slider.y + 53) _pmDrag = slider;
               if(_pmDrag == slider && canDrag && (held || pressed))
               {
                  command = slider.command;
                  sliderValue = int(Math.round(Math.max(0,Math.min(1,(cx - _pmPanel.x - 338) / 132)) * PM_ValueSteps));
               }
            }
            if(resetPressed) command = _pmPage == 0 ? PM_ResetView : PM_ResetEffects;
            if(!held) _pmDrag = null;
            _pmPage = nextPage;
            for each(var pageItem:Object in _pmPageItems) pageItem.view.visible = pageItem.page == _pmPage;
         }
         else if(_pmWasActive)
         {
            for each(var restore:Object in _pmSaved) restore.view.visible = restore.visible;
            _pmSaved = null;
         }
         _pmPanel.visible = active && shown;
         _pmGuide.visible = active && shown && thirds;
         _pmCursor.visible = active && shown && focused;
         _pmWasActive = active;
         // Low six bits: command. Remaining bits: loaded native assets.
         return command + _pmAssets * PM_AssetScale + sliderValue * PM_ValueScale;
      }
