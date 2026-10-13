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
      assert.is_true(setTextEffect(windowName, "lifedrain"))
      assert.is_true(setTextEffect(windowName, "LifeDrain"))
      assert.is_true(setTextEffect(windowName, "lightning"))
      assert.is_true(setTextEffect(windowName, "none"))
      assert.is_true(setTextEffect(windowName, ""))
    end)

    it("should accept every element by its name and by its MudForge adjective", function()
      local names = {
        fire = "flaming", lightning = "shocking", cold = "freezing", acid = "acidic", magic = "arcane",
        poison = "poisonous", drain = "draining", darkness = "unholy", light = "radiant", wind = "gusting",
        water = "drenching", energy = "energized", manadrain = "manadrain", wood = "overgrown", earth = "earthen",
        toxic = "diseased", silver = "silvered", piercing = "nonormal", physical = "normal", bleeding = "bleeding",
        sound = "sonic", mental = "psionic",
      }
      for name, adjective in pairs(names) do
        moveCursorEnd(windowName)
        setTextEffect(windowName, adjective)
        echo(windowName, name)
        resetFormat(windowName)
        selectString(windowName, name, 1)
        assert.are.equal(name, getTextFormat(windowName).effect)
        deselect(windowName)
        clearWindow(windowName)
      end
    end)

    it("should refuse an unknown effect", function()
      local ok, err = setTextEffect(windowName, "nosuchfx")
      assert.is_nil(ok)
      assert.is_truthy(err:find("nosuchfx", 1, true))
    end)

    it("should report a window that does not exist", function()
      local ok, err = setTextEffect("textEffectNoSuchWindow", "lifedrain")
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
      setTextEffect(windowName, "lifedrain")
      echo(windowName, "drained")
      resetFormat(windowName)
      echo(windowName, " after")
      assert.are.equal("none", effectOf("plain"))
      assert.are.equal("lifedrain", effectOf("drained"))
      assert.are.equal("none", effectOf("after"))
    end)

    it("should be carried by what is echoed to the main console", function()
      -- the main console writes through a different path from a miniconsole
      clearWindow()
      moveCursor(0, 0)
      setTextEffect("lifedrain")
      echo("mainfx")
      resetFormat()
      echo(" mainplain")
      selectString("mainfx", 1)
      assert.are.equal("lifedrain", getTextFormat().effect)
      selectString("mainplain", 1)
      assert.are.equal("none", getTextFormat().effect)
      deselect()
    end)

    it("should be applied to the selection", function()
      echo(windowName, "the vampire bites you")
      selectString(windowName, "vampire", 1)
      setTextEffect(windowName, "lifedrain")
      resetFormat(windowName)
      assert.are.equal("lifedrain", effectOf("vampire"))
      assert.are.equal("none", effectOf("bites"))
    end)

    it("should be set and cleared by cecho, decho and hecho tags", function()
      -- one line, as selectString() only searches the current one
      cecho(windowName, "<red><fx:lifedrain>cfx</fx> one ")
      decho(windowName, "<255,0,0><fx:lifedrain>dfx</fx> two ")
      hecho(windowName, "#ff0000#fx(lifedrain)hfx#/fx three")
      assert.are.equal("lifedrain", effectOf("cfx"))
      assert.are.equal("lifedrain", effectOf("dfx"))
      assert.are.equal("lifedrain", effectOf("hfx"))
      assert.are.equal("none", effectOf("one"))
      assert.are.equal("none", effectOf("two"))
      assert.are.equal("none", effectOf("three"))
    end)
  end)

  describe("fx markup", function()
    it("should not leave cecho tags in the text", function()
      cecho(windowName, "<red>The <fx:lifedrain>ancient dragon</fx> roars!")
      assert.are.equal("The ancient dragon roars!", currentLine())
    end)

    it("should not leave decho tags in the text", function()
      decho(windowName, "<255,0,0>The <fx:lifedrain>ancient dragon</fx> roars!")
      assert.are.equal("The ancient dragon roars!", currentLine())
    end)

    it("should not leave hecho tags in the text", function()
      hecho(windowName, "#ff0000The #fx(lifedrain)ancient dragon#/fx roars!")
      assert.are.equal("The ancient dragon roars!", currentLine())
    end)

    it("should leave an unknown effect out of the text and keep echoing", function()
      cecho(windowName, "a<fx:nosuchfx>b</fx>c")
      assert.are.equal("abc", currentLine())
    end)

    it("should drop the tags when echoing to a label", function()
      local labelName = "textEffectLabel"
      createLabel(labelName, 0, 0, 100, 30, 1)
      cecho(labelName, "<fx:lifedrain>drained</fx>")
      assert.is_falsy(getLabelText(labelName):find("fx", 1, true))
      deleteLabel(labelName)
    end)
  end)
end)
