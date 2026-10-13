describe("Tests text effects", function()
  local windowName = "textEffectConsole"

  setup(function()
    createMiniConsole(windowName, 0, 0, 400, 200)
  end)

  teardown(function()
    deleteMiniConsole(windowName)
  end)

  before_each(function()
    clearWindow(windowName)
    moveCursor(windowName, 0, 0)
  end)

  local function currentLine()
    selectCurrentLine(windowName)
    return getSelection(windowName)
  end

  describe("setTextEffect", function()
    it("should accept a known effect, case-insensitively", function()
      assert.is_true(setTextEffect(windowName, "fumes"))
      assert.is_true(setTextEffect(windowName, "FuMes"))
      assert.is_true(setTextEffect(windowName, "bolt"))
      assert.is_true(setTextEffect(windowName, "none"))
      assert.is_true(setTextEffect(windowName, ""))
    end)

    it("should accept every effect by its name, and report it by that name when set by an older one", function()
      local names = {
        bolt = "lightning", flame = "fire", frost = "cold", drip = "bleeding", sparkle = "silver",
        fumes = "toxic", streak = "manadrain", shadow = "darkness", rays = "light", sway = "wind",
        ripple = "water", vine = "wood", dust = "earth", needles = "piercing", impact = "physical",
        waves = "sound", warp = "mental",
      }
      for name, older in pairs(names) do
        for _, given in ipairs({name, older}) do
          moveCursorEnd(windowName)
          assert.is_true(setTextEffect(windowName, given))
          echo(windowName, name)
          resetFormat(windowName)
          selectString(windowName, name, 1)
          assert.are.equal(name, getTextFormat(windowName).effect)
          deselect(windowName)
          clearWindow(windowName)
        end
      end
    end)

    it("should no longer know lifedrain", function()
      assert.is_nil(setTextEffect(windowName, "lifedrain"))
    end)

    it("should refuse an unknown effect", function()
      local ok, err = setTextEffect(windowName, "nosuchfx")
      assert.is_nil(ok)
      assert.is_truthy(err:find("nosuchfx", 1, true))
    end)

    it("should report a window that does not exist", function()
      local ok, err = setTextEffect("textEffectNoSuchWindow", "fumes")
      assert.is_nil(ok)
      assert.is_truthy(err:find("textEffectNoSuchWindow", 1, true))
    end)

    it("should raise for an effect name that is not a string", function()
      assert.has_error(function() setTextEffect(windowName, {}) end)
    end)
  end)

  local function effectOf(text)
    selectString(windowName, text, 1)
    return getTextFormat(windowName).effect
  end

  describe("the effect on the text", function()
    it("should be carried by what is echoed after setTextEffect", function()
      echo(windowName, "plain ")
      setTextEffect(windowName, "fumes")
      echo(windowName, "drained")
      resetFormat(windowName)
      echo(windowName, " after")
      assert.are.equal("none", effectOf("plain"))
      assert.are.equal("fumes", effectOf("drained"))
      assert.are.equal("none", effectOf("after"))
    end)

    it("should be carried by what is echoed to the main console", function()
      -- the main console writes through a different path from a miniconsole
      clearWindow()
      moveCursor(0, 0)
      setTextEffect("fumes")
      echo("mainfx")
      resetFormat()
      echo(" mainplain")
      selectString("mainfx", 1)
      assert.are.equal("fumes", getTextFormat().effect)
      selectString("mainplain", 1)
      assert.are.equal("none", getTextFormat().effect)
      deselect()
    end)

    it("should be applied to the selection", function()
      echo(windowName, "the vampire bites you")
      selectString(windowName, "vampire", 1)
      setTextEffect(windowName, "fumes")
      resetFormat(windowName)
      assert.are.equal("fumes", effectOf("vampire"))
      assert.are.equal("none", effectOf("bites"))
    end)

    it("should be set and cleared by cecho, decho and hecho tags", function()
      -- one line, as selectString() only searches the current one
      cecho(windowName, "<red><fx:fumes>cfx</fx> one ")
      decho(windowName, "<255,0,0><fx:fumes>dfx</fx> two ")
      hecho(windowName, "#ff0000#fx(fumes)hfx#/fx three")
      assert.are.equal("fumes", effectOf("cfx"))
      assert.are.equal("fumes", effectOf("dfx"))
      assert.are.equal("fumes", effectOf("hfx"))
      assert.are.equal("none", effectOf("one"))
      assert.are.equal("none", effectOf("two"))
      assert.are.equal("none", effectOf("three"))
    end)
  end)

  describe("the text effect settings", function()
    local enabled, loops
    before_each(function()
      enabled, loops = getConfig("enableTextEffects"), getConfig("textEffectLoops")
    end)
    after_each(function()
      setConfig("enableTextEffects", enabled)
      setConfig("textEffectLoops", loops)
    end)

    it("should turn effects off and on", function()
      assert.is_true(setConfig("enableTextEffects", false))
      assert.is_false(getConfig("enableTextEffects"))
      assert.is_true(setConfig("enableTextEffects", true))
      assert.is_true(getConfig("enableTextEffects"))
    end)

    it("should keep the effect on the text while they are off", function()
      setConfig("enableTextEffects", false)
      echo(windowName, "the vampire bites you")
      selectString(windowName, "vampire", 1)
      assert.is_true(setTextEffect(windowName, "fumes"))
      resetFormat(windowName)
      assert.are.equal("fumes", effectOf("vampire"))
    end)

    it("should take a number of loops, 0 for forever", function()
      for _, count in ipairs({1, 3, 0, 7}) do
        assert.is_true(setConfig("textEffectLoops", count))
        assert.are.equal(count, getConfig("textEffectLoops"))
      end
    end)

    it("should refuse a negative number of loops", function()
      setConfig("textEffectLoops", 3)
      local ok, err = setConfig("textEffectLoops", -1)
      assert.is_nil(ok)
      assert.is_truthy(err:find("textEffectLoops", 1, true))
      assert.are.equal(3, getConfig("textEffectLoops"))
    end)

    it("should be listed by getConfig()", function()
      local all = getConfig()
      assert.is_not_nil(all.enableTextEffects)
      assert.is_not_nil(all.textEffectLoops)
    end)
  end)

  describe("fx markup", function()
    it("should not leave cecho tags in the text", function()
      cecho(windowName, "<red>The <fx:fumes>ancient dragon</fx> roars!")
      assert.are.equal("The ancient dragon roars!", currentLine())
    end)

    it("should not leave decho tags in the text", function()
      decho(windowName, "<255,0,0>The <fx:fumes>ancient dragon</fx> roars!")
      assert.are.equal("The ancient dragon roars!", currentLine())
    end)

    it("should not leave hecho tags in the text", function()
      hecho(windowName, "#ff0000The #fx(fumes)ancient dragon#/fx roars!")
      assert.are.equal("The ancient dragon roars!", currentLine())
    end)

    it("should leave an unknown effect out of the text and keep echoing", function()
      cecho(windowName, "a<fx:nosuchfx>b</fx>c")
      assert.are.equal("abc", currentLine())
    end)

    it("should drop the tags when echoing to a label", function()
      local labelName = "textEffectLabel"
      createLabel(labelName, 0, 0, 100, 30, 1)
      cecho(labelName, "<fx:fumes>drained</fx>")
      assert.is_falsy(getLabelText(labelName):find("fx", 1, true))
      deleteLabel(labelName)
    end)
  end)
end)
