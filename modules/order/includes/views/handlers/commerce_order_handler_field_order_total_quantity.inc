<?php
// $Id$

/**
 * Field handler to present a link to an order.
 */
class commerce_order_handler_field_order_total_quantity extends views_handler_field {
  function construct() {
    parent::construct();

    $this->additional_fields['order_id'] = 'order_id';
  }

  function query() {
    $this->ensure_my_table();
    $this->add_additional_fields();
  }

  function render($values) {
    $text = !empty($this->options['text']) ? $this->options['text'] : t('view');
    $order_id = $values->{$this->aliases['order_id']};
    
    $order = commerce_order_load($order_id);
    $line_item_ids = array(LANGUAGE_NONE => array());

    foreach ($order->line_items[LANGUAGE_NONE] as $line_item) {
      $line_item_ids[LANGUAGE_NONE][] = array('line_item_id' => $line_item);
    }

    // Add total information and the line item summary links.
    $quantity = commerce_line_items_quantity($line_item_ids);

    return $quantity;
  }
}
