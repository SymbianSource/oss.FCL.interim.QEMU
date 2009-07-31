import qemu

class syborg_serial(qemu.devclass):
  REG_ID           = 0
  REG_DATA         = 1
  REG_FIFO_COUNT   = 2
  REG_INT_ENABLE   = 3
  REG_DMA_TX_ADDR  = 4
  REG_DMA_TX_COUNT = 5 # triggers dma
  REG_DMA_RX_ADDR  = 6
  REG_DMA_RX_COUNT = 7 # triggers dma

  def update_irq(self):
    level = 2 # TX DMA complete
    if len(self.fifo) > 0:
      level |= 1 # FIFO not empty
    if self.dma_rx_count == 0:
      level |= 4 # RX DMA complete
    self.set_irq_level(0, (level & self.int_enable) != 0)

  def can_receive(self):
    return self.fifo_size - len(self.fifo)

  def receive(self, buf):
    for x in buf:
      ch = ord(x)
      if self.dma_rx_count > 0:
        self.dma_writeb(self.dma_rx_addr, ch)
        self.dma_rx_addr += 1
        self.dma_rx_count -= 1
      else:
        self.fifo.append(ch)
    self.update_irq()

  def do_dma_tx(self, count):
    # TODO: Optimize block transmits.
    while count > 0:
      ch = self.dma_readb(self.dma_tx_addr)
      self.chardev.write(ch)
      self.dma_tx_addr += 1
      count -= 1
    self.update_irq()

  def dma_rx_start(self, count):
    while (count > 0) and (len(self.fifo) > 0):
      ch = self.fifo.pop(0)
      self.dma_writeb(self.dma_rx_addr, ch)
      self.dma_rx_addr += 1
      count -= 1
    self.dma_rx_count = count
    self.update_irq()

  def create(self):
    self.fifo_size = self.properties["fifo-size"]
    self.chardev = self.properties["chardev"]
    self.fifo=[]
    self.int_enable = 0
    self.chardev.set_handlers(self.can_receive, self.receive)
    self.dma_tx_addr = 0
    self.dma_rx_addr = 0
    self.dma_rx_count = 0

  def read_reg(self, offset):
    offset >>= 2
    if offset == self.REG_ID:
      return 0xc51d0001
    elif offset == self.REG_DATA:
      if len(self.fifo) == 0:
        return 0xffffffff
      val = self.fifo.pop(0)
      self.update_irq();
      return val
    elif offset == self.REG_FIFO_COUNT:
      return len(self.fifo)
    elif offset == self.REG_INT_ENABLE:
      return self.int_enable
    elif offset == self.REG_DMA_TX_ADDR:
      return self.dma_tx_addr
    elif offset == self.REG_DMA_TX_COUNT:
      return 0
    elif offset == self.REG_DMA_RX_ADDR:
      return self.dma_rx_addr
    elif offset == self.REG_DMA_RX_COUNT:
      return self.dma_rx_count
    return 0

  def write_reg(self, offset, value):
    offset >>= 2
    if offset == self.REG_DATA:
      self.chardev.write(value)
    elif offset == self.REG_INT_ENABLE:
      self.int_enable = value & 7
      self.update_irq()
    elif offset == self.REG_DMA_TX_ADDR:
      self.dma_tx_addr = value
    elif offset == self.REG_DMA_TX_COUNT:
      self.do_dma_tx(value)
    elif offset == self.REG_DMA_RX_ADDR:
      self.dma_rx_addr = value
    elif offset == self.REG_DMA_RX_COUNT:
      self.dma_rx_start(value)

  def save(self, f):
    f.put_u32(self.fifo_size)
    f.put_u32(self.int_enable)
    f.put_u32(self.dma_tx_addr)
    f.put_u32(self.dma_rx_addr)
    f.put_u32(self.dma_rx_count)
    f.put_u32(len(self.fifo))
    for x in self.fifo:
      f.put_u32(x)

  def load(self, f):
    if self.fifo_size != f.get_u32():
      raise ValueError, "fifo size mismatch"
    self.int_enable = f.get_u32()
    self.dma_tx_addr = f.get_u32()
    self.dma_rx_addr = f.get_u32()
    self.dma_rx_count = f.get_u32()
    n = f.get_u32()
    self.fifo = []
    while n > 0:
      self.fifo.append(f.get_u32())
      n -= 1;

  # Device class properties
  regions = [qemu.ioregion(0x1000, readl=read_reg, writel=write_reg)]
  irqs = 1
  name = "syborg,serial"
  properties = {"fifo-size":16, "chardev":None}

qemu.register_device(syborg_serial)
